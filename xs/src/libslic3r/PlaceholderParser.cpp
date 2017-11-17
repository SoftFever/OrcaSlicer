#include "PlaceholderParser.hpp"
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#ifdef _MSC_VER
    #include <stdlib.h>  // provides **_environ
#else
    #include <unistd.h>  // provides **environ
#endif

#ifdef __APPLE__
#include <crt_externs.h>
#undef environ
#define environ (*_NSGetEnviron())
#else
    #ifdef _MSC_VER
       #define environ _environ
    #else
     	extern char **environ;
    #endif
#endif

// Spirit v2.5 allows you to suppress automatic generation
// of predefined terminals to speed up complation. With
// BOOST_SPIRIT_NO_PREDEFINED_TERMINALS defined, you are
// responsible in creating instances of the terminals that
// you need (e.g. see qi::uint_type uint_ below).
//#define BOOST_SPIRIT_NO_PREDEFINED_TERMINALS

#include <boost/config/warning_disable.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/qi_lit.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_fusion.hpp>
#include <boost/spirit/include/phoenix_stl.hpp>
#include <boost/spirit/include/phoenix_object.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/variant/recursive_variant.hpp>
#include <boost/phoenix/bind/bind_function.hpp>

#include <iostream>
#include <string>

namespace Slic3r {

PlaceholderParser::PlaceholderParser()
{
    this->set("version", SLIC3R_VERSION);
    this->apply_env_variables();
    this->update_timestamp();
}

void PlaceholderParser::update_timestamp()
{
    time_t rawtime;
    time(&rawtime);
    struct tm* timeinfo = localtime(&rawtime);
    
    {
        std::ostringstream ss;
        ss << (1900 + timeinfo->tm_year);
        ss << std::setw(2) << std::setfill('0') << (1 + timeinfo->tm_mon);
        ss << std::setw(2) << std::setfill('0') << timeinfo->tm_mday;
        ss << "-";
        ss << std::setw(2) << std::setfill('0') << timeinfo->tm_hour;
        ss << std::setw(2) << std::setfill('0') << timeinfo->tm_min;
        ss << std::setw(2) << std::setfill('0') << timeinfo->tm_sec;
        this->set("timestamp", ss.str());
    }
    this->set("year",   1900 + timeinfo->tm_year);
    this->set("month",  1 + timeinfo->tm_mon);
    this->set("day",    timeinfo->tm_mday);
    this->set("hour",   timeinfo->tm_hour);
    this->set("minute", timeinfo->tm_min);
    this->set("second", timeinfo->tm_sec);
}

// Scalar configuration values are stored into m_single,
// vector configuration values are stored into m_multiple.
// All vector configuration values stored into the PlaceholderParser
// are expected to be addressed by the extruder ID, therefore
// if a vector configuration value is addressed without an index,
// a current extruder ID is used.
void PlaceholderParser::apply_config(const DynamicPrintConfig &rhs)
{
    const ConfigDef *def = rhs.def();
    for (const t_config_option_key &opt_key : rhs.keys()) {
        const ConfigOptionDef *opt_def = def->get(opt_key);
        if (opt_def->multiline || opt_key == "post_process")
            continue;
        const ConfigOption *opt = rhs.option(opt_key);
        // Store a copy of the config option.
        // Convert FloatOrPercent values to floats first.
        //FIXME there are some ratio_over chains, which end with empty ratio_with.
        // For example, XXX_extrusion_width parameters are not handled by get_abs_value correctly.
        this->set(opt_key, (opt->type() == coFloatOrPercent) ?
            new ConfigOptionFloat(rhs.get_abs_value(opt_key)) :
            opt->clone());
    }
}

void PlaceholderParser::apply_env_variables()
{
    for (char** env = environ; *env; ++ env) {
        if (strncmp(*env, "SLIC3R_", 7) == 0) {
            std::stringstream ss(*env);
            std::string key, value;
            std::getline(ss, key, '=');
            ss >> value;
            this->set(key, value);
        }
    }
}

namespace client
{
    struct MyContext {
        const PlaceholderParser *pp = nullptr;
        const DynamicConfig     *config_override = nullptr;
        const size_t             current_extruder_id = 0;

        const ConfigOption*     resolve_symbol(const std::string &opt_key) const
        {
            const ConfigOption *opt = nullptr;
            if (config_override != nullptr)
                opt = config_override->option(opt_key);
            if (opt == nullptr)
                opt = pp->option(opt_key);
            return opt;
        }

        template <typename Iterator>
        static void legacy_variable_expansion(
            const MyContext                 *ctx, 
            boost::iterator_range<Iterator> &opt_key,
            std::string                     &output)
        {
            std::string         opt_key_str(opt_key.begin(), opt_key.end());
            const ConfigOption *opt = ctx->resolve_symbol(opt_key_str);
            size_t              idx = ctx->current_extruder_id;
            if (opt == nullptr) {
                // Check whether this is a legacy vector indexing.
                idx = opt_key_str.rfind('_');
                if (idx != std::string::npos) {
                    opt = ctx->resolve_symbol(opt_key_str.substr(0, idx));
                    if (opt != nullptr) {
                        if (! opt->is_vector())
                            boost::throw_exception(boost::spirit::qi::expectation_failure<Iterator>(
                                opt_key.begin(), opt_key.end(), boost::spirit::info("Trying to index a scalar variable")));
                        char *endptr = nullptr;
                        idx = strtol(opt_key_str.c_str() + idx + 1, &endptr, 10);
                        if (endptr == nullptr || *endptr != 0)
                            boost::throw_exception(boost::spirit::qi::expectation_failure<Iterator>(
                                opt_key.begin() + idx + 1, opt_key.end(), boost::spirit::info("Invalid vector index")));
                    }
                }
            }
            if (opt == nullptr)
                boost::throw_exception(boost::spirit::qi::expectation_failure<Iterator>(
                    opt_key.begin(), opt_key.end(), boost::spirit::info("Variable does not exist")));
            if (opt->is_scalar())
                output = opt->serialize();
            else {
                const ConfigOptionVectorBase *vec = static_cast<const ConfigOptionVectorBase*>(opt);
                if (vec->empty())
                    boost::throw_exception(boost::spirit::qi::expectation_failure<Iterator>(
                        opt_key.begin(), opt_key.end(), boost::spirit::info("Indexing an empty vector variable")));
                output = vec->vserialize()[(idx >= vec->size()) ? 0 : idx];
            }
        }

        template <typename Iterator>
        static void legacy_variable_expansion2(
            const MyContext                 *ctx, 
            boost::iterator_range<Iterator> &opt_key,
            boost::iterator_range<Iterator> &opt_vector_index,
            std::string                     &output)
        {
            std::string         opt_key_str(opt_key.begin(), opt_key.end());
            const ConfigOption *opt = ctx->resolve_symbol(opt_key_str);
            if (opt == nullptr) {
                // Check whether the opt_key ends with '_'.
                if (opt_key_str.back() == '_')
                    opt_key_str.resize(opt_key_str.size() - 1);
                opt = ctx->resolve_symbol(opt_key_str);
            }
            if (! opt->is_vector())
                boost::throw_exception(boost::spirit::qi::expectation_failure<Iterator>(
                    opt_key.begin(), opt_key.end(), boost::spirit::info("Trying to index a scalar variable")));
            const ConfigOptionVectorBase *vec = static_cast<const ConfigOptionVectorBase*>(opt);
            if (vec->empty())
                boost::throw_exception(boost::spirit::qi::expectation_failure<Iterator>(
                    opt_key.begin(), opt_key.end(), boost::spirit::info("Indexing an empty vector variable")));
            const ConfigOption *opt_index = ctx->resolve_symbol(std::string(opt_vector_index.begin(), opt_vector_index.end()));
            if (opt_index == nullptr)
                boost::throw_exception(boost::spirit::qi::expectation_failure<Iterator>(
                    opt_key.begin(), opt_key.end(), boost::spirit::info("Variable does not exist")));
            if (opt_index->type() != coInt)
                boost::throw_exception(boost::spirit::qi::expectation_failure<Iterator>(
                    opt_key.begin(), opt_key.end(), boost::spirit::info("Indexing variable has to be integer")));
			int idx = opt_index->getInt();
			if (idx < 0)
				boost::throw_exception(boost::spirit::qi::expectation_failure<Iterator>(
					opt_key.begin(), opt_key.end(), boost::spirit::info("Negative vector index")));
			output = vec->vserialize()[(idx >= (int)vec->size()) ? 0 : idx];
        }
    };

    struct expr
    {
        expr(int i = 0) : type(TYPE_INT) { data.i = i; }
        expr(double d) : type(TYPE_DOUBLE) { data.d = d; }
        expr(const char *s) : type(TYPE_STRING) { data.s = new std::string(s); }
        expr(std::string &s) : type(TYPE_STRING) { data.s = new std::string(s); }
        expr(const expr &rhs) : type(rhs.type) { data.s = (rhs.type == TYPE_STRING) ? new std::string(*rhs.data.s) : rhs.data.s; }
        expr(expr &&rhs) : type(rhs.type) { data.s = rhs.data.s; rhs.data.s = nullptr; rhs.type = TYPE_EMPTY; }
        ~expr() { if (type == TYPE_STRING) delete data.s; data.s = nullptr; }

        expr &operator=(const expr &rhs) 
        { 
            type = rhs.type; 
            data.s = (type == TYPE_STRING) ? new std::string(*rhs.data.s) : rhs.data.s; 
            return *this; 
        }

        expr &operator=(expr &&rhs) 
        { 
            type = rhs.type; 
            data.s = rhs.data.s; 
            rhs.data.s = nullptr; 
            rhs.type = TYPE_EMPTY; 
            return *this; 
        }

        int&                i()       { return data.i; }
        int                 i() const { return data.i; }
        double&             d()       { return data.d; }
        double              d() const { return data.d; }
        std::string&        s()       { return *data.s; }
        const std::string&  s() const { return *data.s; }
        
        std::string         to_string() const 
        {
            std::string out;
            switch (type) {
            case TYPE_INT:    out = boost::to_string(data.i); break;
            case TYPE_DOUBLE: out = boost::to_string(data.d); break;
            case TYPE_STRING: out = *data.s; break;
            }
            return out;
        }

        union {
            int          i;
            double       d;
            std::string *s;
        } data;

        enum Type {
            TYPE_EMPTY = 0,
            TYPE_INT,
            TYPE_DOUBLE,
            TYPE_STRING,
        };

        int type;
    };

    ///////////////////////////////////////////////////////////////////////////
    //  Our calculator grammar
    ///////////////////////////////////////////////////////////////////////////
    template <typename Iterator>
    struct calculator : boost::spirit::qi::grammar<Iterator, std::string(const MyContext*), boost::spirit::ascii::space_type>
    {
        calculator() : calculator::base_type(start)
        {
            using boost::spirit::qi::alpha;
            using boost::spirit::qi::alnum;
            using boost::spirit::qi::eol;
            using boost::spirit::qi::eoi;
            using boost::spirit::qi::eps;
            using boost::spirit::qi::raw;
            using boost::spirit::qi::lit;
            using boost::spirit::qi::lexeme;
            using boost::spirit::qi::on_error;
            using boost::spirit::qi::fail;
            using boost::spirit::ascii::char_;
            using boost::spirit::int_;
            using boost::spirit::double_;
            using boost::spirit::ascii::string;
            using namespace boost::spirit::qi::labels;

            using boost::phoenix::construct;
            using boost::phoenix::val;
            using boost::phoenix::begin;

            boost::spirit::qi::_val_type _val;
            boost::spirit::qi::_1_type _1;
            boost::spirit::qi::_2_type _2;
            boost::spirit::qi::_r1_type _r1;
            boost::spirit::qi::uint_type uint_;

            // Starting symbol of the grammer.
            // The leading eps is required by the "expectation point" operator ">".
            // Without it, some of the errors would not trigger the error handler.
            start = eps > *(text [_val+=_1]
                || ((lit('{') > macro [_val+=_1] > '}') 
                | (lit('[') > legacy_variable_expansion(_r1) [_val+=_1] > ']')));
            start.name("start");

            // Free-form text up to a first brace, including spaces and newlines.
            // The free-form text will be inserted into the processed text without a modification.
            text = raw[+(char_ - '[' - '{')];
            text.name("text");

            // New style of macro expansion.
            // The macro expansion may contain numeric or string expressions, ifs and cases.
            macro = identifier;
            macro.name("macro");

            // Legacy variable expansion of the original Slic3r, in the form of [scalar_variable] or [vector_variable_index].
            legacy_variable_expansion =
                    (identifier >> &lit(']'))
                        [ boost::phoenix::bind(&MyContext::legacy_variable_expansion<Iterator>, _r1, _1, _val) ]
                |   (identifier > lit('[') > identifier > ']') 
                        [ boost::phoenix::bind(&MyContext::legacy_variable_expansion2<Iterator>, _r1, _1, _2, _val) ]
                ;
            legacy_variable_expansion.name("legacy_variable_expansion");

            identifier =
//                    !expr.keywords >>  
                raw[lexeme[(alpha | '_') >> *(alnum | '_')]]
                ;
            identifier.name("identifier");

/*
            bool_expr =
                (expression >> '=' >> '=' >> expression) |
                (expression >> '!' >> '=' >> expression) |
                (expression >> '<' >> '>' >> expression)
                ;

            expression =
                term                            //[_val = _1]
                >> *(   ('+' >> term            ) //[_val += _1])
                    |   ('-' >> term            )//[_val -= _1])
                    )
                ;

            term =
                factor                          //[_val = _1]
                >> *(   ('*' >> factor          )//[_val *= _1])
                    |   ('/' >> factor          )//[_val /= _1])
                    )
                ;

            factor =
                    int_                        //[_val = expr(_1)]
                |   double_                     //[_val = expr(_1)]
                |   '(' >> expression           >> ')' // [_val = std::move(_1)] >> ')'
                |   ('-' >> factor              ) //[_val = -_1])
                |   ('+' >> factor              ) //[_val = std::move(_1)])
                ;
*/

//            text %= lexeme[+(char_ - '<')];

//            text_to_eol %= lexeme[*(char_ - eol) >> (eol | eoi)];
            /*
            expression_with_braces = lit('{') >> (
                string("if")     >> if_else_output [_val = _1] |
                string("switch") >> switch_output  [_val = _1] |
                expression [_val = boost::to_string(_1)] >> '}'
                );
            if_else_output = 
                bool_expr[_r1 = _1] >> '}' >> text_to_eol[_val = _r1 ? _1 : std::string()] >>
                    *(lit('{') >> "elsif" >> bool_expr[_r1 = !_r1 && _1] >> '}' >> text_to_eol[_val = _r1 ? _1 : std::string()]) >>
                    -(lit('{') >> "else" >> '}' >> text_to_eol[_val = _r1 ? std::string() : _1]);
*/
/*
            on_error<fail>(start, 
                    phx::ref(std::cout)
                       << "Error! Expecting "
                       << boost::spirit::qi::_4
                       << " here: '"
                       << construct<std::string>(boost::spirit::qi::_3, boost::spirit::qi::_2)
                       << "'\n"
                );
*/
        }

        // The start of the grammar.
        boost::spirit::qi::rule<Iterator, std::string(const MyContext*), boost::spirit::ascii::space_type> start;
        // A free-form text.
        boost::spirit::qi::rule<Iterator, std::string(), boost::spirit::ascii::space_type> text;
        // Statements enclosed in curely braces {}
        boost::spirit::qi::rule<Iterator, std::string(), boost::spirit::ascii::space_type> macro;
        // Legacy variable expansion of the original Slic3r, in the form of [scalar_variable] or [vector_variable_index].
        boost::spirit::qi::rule<Iterator, std::string(const MyContext*), boost::spirit::ascii::space_type> legacy_variable_expansion;
        // Parsed identifier name.
        boost::spirit::qi::rule<Iterator,  boost::iterator_range<Iterator>, boost::spirit::ascii::space_type> identifier;

        boost::spirit::qi::rule<Iterator, expr(), boost::spirit::ascii::space_type> expression, term, factor;
        boost::spirit::qi::rule<Iterator, std::string(), boost::spirit::ascii::space_type> text_to_eol;
        boost::spirit::qi::rule<Iterator, std::string(bool), boost::spirit::ascii::space_type> expression_with_braces;
        boost::spirit::qi::rule<Iterator, bool, boost::spirit::ascii::space_type> bool_expr;

        boost::spirit::qi::rule<Iterator, std::string(bool), boost::spirit::ascii::space_type> if_else_output;
        boost::spirit::qi::rule<Iterator, std::string(expr), boost::spirit::ascii::space_type> switch_output;
    };
}

struct printer
{
    typedef boost::spirit::utf8_string string;

    void element(string const& tag, string const& value, int depth) const
    {
        for (int i = 0; i < (depth*4); ++i) // indent to depth
            std::cout << ' ';
        std::cout << "tag: " << tag;
        if (value != "")
            std::cout << ", value: " << value;
        std::cout << std::endl;
    }
};

void print_info(boost::spirit::info const& what)
{
    using boost::spirit::basic_info_walker;
    printer pr;
    basic_info_walker<printer> walker(pr, what.tag, 0);
    boost::apply_visitor(walker, what.value);
}

std::string PlaceholderParser::process(const std::string &templ, unsigned int current_extruder_id, const DynamicConfig *config_override) const
{
    typedef std::string::const_iterator iterator_type;
    typedef client::calculator<iterator_type> calculator;

    boost::spirit::ascii::space_type space; // Our skipper
    calculator calc; // Our grammar

    std::string::const_iterator iter = templ.begin();
    std::string::const_iterator end = templ.end();
    //std::string result;
    std::string result;
    bool r = false;
    try {
        client::MyContext context;
        context.pp = this;
        context.config_override = config_override;
        r = phrase_parse(iter, end, calc(&context), space, result);
    } catch (boost::spirit::qi::expectation_failure<iterator_type> const& x) {
        std::cout << "expected: "; print_info(x.what_);
        std::cout << "got: \"" << std::string(x.first, x.last) << '"' << std::endl;
    }

    if (r && iter == end)
    {
//        std::cout << "-------------------------\n";
//        std::cout << "Parsing succeeded\n";
//        std::cout << "result = " << result << std::endl;
//        std::cout << "-------------------------\n";
    }
    else
    {
        std::string rest(iter, end);
        std::cout << "-------------------------\n";
        std::cout << "Parsing failed\n";
        std::cout << "stopped at: \" " << rest << "\"\n";
        std::cout << "source: \n" << templ;       
        std::cout << "-------------------------\n";
    }
    return result;
}

}
