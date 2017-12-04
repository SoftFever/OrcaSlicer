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

#include <boost/algorithm/string.hpp>
// Unicode iterator to iterate over utf8.
#include <boost/regex/pending/unicode_iterator.hpp>

// Spirit v2.5 allows you to suppress automatic generation
// of predefined terminals to speed up complation. With
// BOOST_SPIRIT_NO_PREDEFINED_TERMINALS defined, you are
// responsible in creating instances of the terminals that
// you need (e.g. see qi::uint_type uint_ below).
//#define BOOST_SPIRIT_NO_PREDEFINED_TERMINALS

#define BOOST_RESULT_OF_USE_DECLTYPE
#define BOOST_SPIRIT_USE_PHOENIX_V3
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
#include <boost/spirit/repository/include/qi_distinct.hpp>
#include <boost/spirit/repository/include/qi_iter_pos.hpp>
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
        if ((opt_def->multiline && boost::ends_with(opt_key, "_gcode")) || opt_key == "post_process")
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

namespace spirit = boost::spirit;
namespace qi = boost::spirit::qi;
namespace px = boost::phoenix;

namespace client
{
    template<typename Iterator>
    struct OptWithPos {
        OptWithPos() {}
        OptWithPos(ConfigOptionConstPtr opt, boost::iterator_range<Iterator> it_range) : opt(opt), it_range(it_range) {}
        ConfigOptionConstPtr             opt = nullptr;
        boost::iterator_range<Iterator>  it_range;
    };

    template<typename ITERATOR>
    std::ostream& operator<<(std::ostream& os, OptWithPos<ITERATOR> const& opt)
    {
        os << std::string(opt.it_range.begin(), opt.it_range.end());
        return os;
    }

    template<typename Iterator>
    struct expr
    {
                 expr() : type(TYPE_EMPTY) {}
        explicit expr(bool b) : type(TYPE_BOOL) { data.b = b; }
        explicit expr(bool b, const Iterator &it_begin, const Iterator &it_end) : type(TYPE_BOOL), it_range(it_begin, it_end) { data.b = b; }
        explicit expr(int i) : type(TYPE_INT) { data.i = i; }
        explicit expr(int i, const Iterator &it_begin, const Iterator &it_end) : type(TYPE_INT), it_range(it_begin, it_end) { data.i = i; }
        explicit expr(double d) : type(TYPE_DOUBLE) { data.d = d; }
        explicit expr(double d, const Iterator &it_begin, const Iterator &it_end) : type(TYPE_DOUBLE), it_range(it_begin, it_end) { data.d = d; }
        explicit expr(const char *s) : type(TYPE_STRING) { data.s = new std::string(s); }
        explicit expr(const std::string &s) : type(TYPE_STRING) { data.s = new std::string(s); }
        explicit expr(const std::string &s, const Iterator &it_begin, const Iterator &it_end) : 
            type(TYPE_STRING), it_range(it_begin, it_end) { data.s = new std::string(s); }
                 expr(const expr &rhs) : type(rhs.type), it_range(rhs.it_range)
            { if (rhs.type == TYPE_STRING) data.s = new std::string(*rhs.data.s); else data.set(rhs.data); }
        explicit expr(expr &&rhs) : type(rhs.type), it_range(rhs.it_range)
            { data.set(rhs.data); rhs.type = TYPE_EMPTY; }
        explicit expr(expr &&rhs, const Iterator &it_begin, const Iterator &it_end) : type(rhs.type), it_range(it_begin, it_end)
            { data.set(rhs.data); rhs.type = TYPE_EMPTY; }
        ~expr() { this->reset(); }

        expr &operator=(const expr &rhs) 
        { 
            this->type      = rhs.type;
            this->it_range  = rhs.it_range;
            if (rhs.type == TYPE_STRING) 
                this->data.s = new std::string(*rhs.data.s);
            else 
                this->data.set(rhs.data);
            return *this; 
        }

        expr &operator=(expr &&rhs) 
        { 
            type            = rhs.type;
            this->it_range  = rhs.it_range;
            data.set(rhs.data);
            rhs.type        = TYPE_EMPTY;
            return *this;
        }

        void                reset()   
        { 
            if (this->type == TYPE_STRING) 
                delete data.s;
            this->type = TYPE_EMPTY;
        }

        bool&               b()       { return data.b; }
        bool                b() const { return data.b; }
        void                set_b(bool v) { this->reset(); this->data.b = v; this->type = TYPE_BOOL; }
        int&                i()       { return data.i; }
        int                 i() const { return data.i; }
        void                set_i(int v) { this->reset(); this->data.i = v; this->type = TYPE_INT; }
        int                 as_i() const { return (this->type == TYPE_INT) ? this->i() : int(this->d()); }
        double&             d()       { return data.d; }
        double              d() const { return data.d; }
        void                set_d(double v) { this->reset(); this->data.d = v; this->type = TYPE_DOUBLE; }
        double              as_d() const { return (this->type == TYPE_DOUBLE) ? this->d() : double(this->i()); }
        std::string&        s()       { return *data.s; }
        const std::string&  s() const { return *data.s; }
        void                set_s(const std::string &s) { this->reset(); this->data.s = new std::string(s); this->type = TYPE_STRING; }
        void                set_s(std::string &&s) { this->reset(); this->data.s = new std::string(std::move(s)); this->type = TYPE_STRING; }
        
        std::string         to_string() const 
        {
            std::string out;
            switch (type) {
            case TYPE_BOOL:   out = boost::to_string(data.b); break;
            case TYPE_INT:    out = boost::to_string(data.i); break;
            case TYPE_DOUBLE: out = boost::to_string(data.d); break;
            case TYPE_STRING: out = *data.s; break;
            default:          break;
            }
            return out;
        }

        union Data {
            // Raw image of the other data members.
            // The C++ compiler will consider a possible aliasing of char* with any other union member,
            // therefore copying the raw data is safe.
            char         raw[8];
            bool         b;
            int          i;
            double       d;
            std::string *s;

            // Copy the largest member variable through char*, which will alias with all other union members by default.
            void set(const Data &rhs) { memcpy(this->raw, rhs.raw, sizeof(rhs.raw)); }
        } data;

        enum Type {
            TYPE_EMPTY = 0,
            TYPE_BOOL,
            TYPE_INT,
            TYPE_DOUBLE,
            TYPE_STRING,
        };

        Type type;

        // Range of input iterators covering this expression.
        // Used for throwing parse exceptions.
        boost::iterator_range<Iterator>  it_range;

        expr unary_minus(const Iterator start_pos) const
        { 
            switch (this->type) {
            case TYPE_INT :
                return expr<Iterator>(- this->i(), start_pos, this->it_range.end());
            case TYPE_DOUBLE:
                return expr<Iterator>(- this->d(), start_pos, this->it_range.end()); 
            default:
                this->throw_exception("Cannot apply unary minus operator.");
            }
            assert(false);
            // Suppress compiler warnings.
            return expr();
        }

        expr unary_not(const Iterator start_pos) const
        { 
            switch (this->type) {
            case TYPE_BOOL :
                return expr<Iterator>(! this->b(), start_pos, this->it_range.end());
            default:
                this->throw_exception("Cannot apply a not operator.");
            }
            assert(false);
            // Suppress compiler warnings.
            return expr();
        }

        expr &operator+=(const expr &rhs)
        { 
            const char *err_msg = "Cannot multiply with non-numeric type.";
            this->throw_if_not_numeric(err_msg);
            rhs.throw_if_not_numeric(err_msg);
            if (this->type == TYPE_DOUBLE || rhs.type == TYPE_DOUBLE) {
                double d = this->as_d() + rhs.as_d();
                this->data.d = d;
                this->type = TYPE_DOUBLE;
            } else
                this->data.i += rhs.i();
            this->it_range = boost::iterator_range<Iterator>(this->it_range.begin(), rhs.it_range.end());
            return *this;
        }

        expr &operator-=(const expr &rhs)
        { 
            const char *err_msg = "Cannot multiply with non-numeric type.";
            this->throw_if_not_numeric(err_msg);
            rhs.throw_if_not_numeric(err_msg);
            if (this->type == TYPE_DOUBLE || rhs.type == TYPE_DOUBLE) {
                double d = this->as_d() - rhs.as_d();
                this->data.d = d;
                this->type = TYPE_DOUBLE;
            } else
                this->data.i -= rhs.i();
            this->it_range = boost::iterator_range<Iterator>(this->it_range.begin(), rhs.it_range.end());
            return *this;
        }

        expr &operator*=(const expr &rhs)
        { 
            const char *err_msg = "Cannot multiply with non-numeric type.";
            this->throw_if_not_numeric(err_msg);
            rhs.throw_if_not_numeric(err_msg);
            if (this->type == TYPE_DOUBLE || rhs.type == TYPE_DOUBLE) {
                double d = this->as_d() * rhs.as_d();
                this->data.d = d;
                this->type = TYPE_DOUBLE;
            } else
                this->data.i *= rhs.i();
            this->it_range = boost::iterator_range<Iterator>(this->it_range.begin(), rhs.it_range.end());
            return *this;
        }

        expr &operator/=(const expr &rhs)
        {
            this->throw_if_not_numeric("Cannot divide a non-numeric type.");
            rhs.throw_if_not_numeric("Cannot divide with a non-numeric type.");
            if ((this->type == TYPE_INT) ? (rhs.i() == 0) : (rhs.d() == 0.))
                rhs.throw_exception("Division by zero");
            if (this->type == TYPE_DOUBLE || rhs.type == TYPE_DOUBLE) {
                double d = this->as_d() / rhs.as_d();
                this->data.d = d;
                this->type = TYPE_DOUBLE;
            } else
                this->data.i /= rhs.i();
            this->it_range = boost::iterator_range<Iterator>(this->it_range.begin(), rhs.it_range.end());
            return *this;
        }

        static void to_string2(expr &self, std::string &out)
        {
            out = self.to_string();
        }

        static void evaluate_boolean(expr &self, bool &out)
        {
            if (self.type != TYPE_BOOL)
                self.throw_exception("Not a boolean expression");
            out = self.b();
        }

        // Is lhs==rhs? Store the result into lhs.
        static void compare_op(expr &lhs, expr &rhs, char op)
        {
            bool value = false;
            if ((lhs.type == TYPE_INT || lhs.type == TYPE_DOUBLE) &&
                (rhs.type == TYPE_INT || rhs.type == TYPE_DOUBLE)) {
                // Both types are numeric.
                value = (lhs.type == TYPE_DOUBLE || rhs.type == TYPE_DOUBLE) ? 
                    (lhs.as_d() == rhs.as_d()) : (lhs.i() == rhs.i());
            } else if (lhs.type == TYPE_BOOL && rhs.type == TYPE_BOOL) {
                // Both type are bool.
                value = lhs.b() == rhs.b();
            } else if (lhs.type == TYPE_STRING || rhs.type == TYPE_STRING) {
                // One type is string, the other could be converted to string.
                value = lhs.to_string() == rhs.to_string();
            } else {
                boost::throw_exception(qi::expectation_failure<Iterator>(
                    lhs.it_range.begin(), rhs.it_range.end(), spirit::info("Cannot compare the types.")));
            }
            lhs.type = TYPE_BOOL;
            lhs.data.b = (op == '=') ? value : !value;
        }
        static void equal(expr &lhs, expr &rhs) { compare_op(lhs, rhs, '='); }
        static void not_equal(expr &lhs, expr &rhs) { compare_op(lhs, rhs, '!'); }

        static void set_if(bool &cond, bool &not_yet_consumed, std::string &str_in, std::string &str_out)
        {
            if (cond && not_yet_consumed) {
                str_out = str_in;
                not_yet_consumed = false;
            }
        }

        void throw_exception(const char *message) const 
        {
            boost::throw_exception(qi::expectation_failure<Iterator>(
                this->it_range.begin(), this->it_range.end(), spirit::info(message)));
        }

        void throw_if_not_numeric(const char *message) const 
        {
            if (this->type != TYPE_INT && this->type != TYPE_DOUBLE)
                this->throw_exception(message);
        }
    };

    template<typename ITERATOR>
    std::ostream& operator<<(std::ostream &os, const expr<ITERATOR> &expression)
    {
        typedef expr<ITERATOR> Expr;
        os << std::string(expression.it_range.begin(), expression.it_range.end()) << " - ";
        switch (expression.type) {
        case Expr::TYPE_EMPTY:    os << "empty"; break;
        case Expr::TYPE_BOOL:     os << "bool ("   << expression.b() << ")"; break;
        case Expr::TYPE_INT:      os << "int ("    << expression.i() << ")"; break;
        case Expr::TYPE_DOUBLE:   os << "double (" << expression.d() << ")"; break;
        case Expr::TYPE_STRING:   os << "string (" << expression.s() << ")"; break;
        default: os << "unknown";
        };
        return os;
    }

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
                            boost::throw_exception(qi::expectation_failure<Iterator>(
                                opt_key.begin(), opt_key.end(), spirit::info("Trying to index a scalar variable")));
                        char *endptr = nullptr;
                        idx = strtol(opt_key_str.c_str() + idx + 1, &endptr, 10);
                        if (endptr == nullptr || *endptr != 0)
                            boost::throw_exception(qi::expectation_failure<Iterator>(
                                opt_key.begin() + idx + 1, opt_key.end(), spirit::info("Invalid vector index")));
                    }
                }
            }
            if (opt == nullptr)
                boost::throw_exception(qi::expectation_failure<Iterator>(
                    opt_key.begin(), opt_key.end(), spirit::info("Variable does not exist")));
            if (opt->is_scalar())
                output = opt->serialize();
            else {
                const ConfigOptionVectorBase *vec = static_cast<const ConfigOptionVectorBase*>(opt);
                if (vec->empty())
                    boost::throw_exception(qi::expectation_failure<Iterator>(
                        opt_key.begin(), opt_key.end(), spirit::info("Indexing an empty vector variable")));
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
                boost::throw_exception(qi::expectation_failure<Iterator>(
                    opt_key.begin(), opt_key.end(), spirit::info("Trying to index a scalar variable")));
            const ConfigOptionVectorBase *vec = static_cast<const ConfigOptionVectorBase*>(opt);
            if (vec->empty())
                boost::throw_exception(qi::expectation_failure<Iterator>(
                    opt_key.begin(), opt_key.end(), spirit::info("Indexing an empty vector variable")));
            const ConfigOption *opt_index = ctx->resolve_symbol(std::string(opt_vector_index.begin(), opt_vector_index.end()));
            if (opt_index == nullptr)
                boost::throw_exception(qi::expectation_failure<Iterator>(
                    opt_key.begin(), opt_key.end(), spirit::info("Variable does not exist")));
            if (opt_index->type() != coInt)
                boost::throw_exception(qi::expectation_failure<Iterator>(
                    opt_key.begin(), opt_key.end(), spirit::info("Indexing variable has to be integer")));
			int idx = opt_index->getInt();
			if (idx < 0)
				boost::throw_exception(qi::expectation_failure<Iterator>(
					opt_key.begin(), opt_key.end(), spirit::info("Negative vector index")));
			output = vec->vserialize()[(idx >= (int)vec->size()) ? 0 : idx];
        }

        template <typename Iterator>
        static void resolve_variable(
            const MyContext                 *ctx,
            boost::iterator_range<Iterator> &opt_key,
            OptWithPos<Iterator>            &output)
        {
            const ConfigOption *opt = ctx->resolve_symbol(std::string(opt_key.begin(), opt_key.end()));
            if (opt == nullptr)
                boost::throw_exception(qi::expectation_failure<Iterator>(
                    opt_key.begin(), opt_key.end(), spirit::info("Not a variable name")));
            output.opt = opt;
            output.it_range = opt_key;
        }

        template <typename Iterator>
        static void scalar_variable_reference(
            const MyContext                 *ctx,
            OptWithPos<Iterator>            &opt,
            expr<Iterator>                  &output)
        {
            if (opt.opt->is_vector())
                boost::throw_exception(qi::expectation_failure<Iterator>(
                    opt.it_range.begin(), opt.it_range.end(), spirit::info("Referencing a scalar variable in a vector context")));
            switch (opt.opt->type()) {
            case coFloat:   output.set_d(opt.opt->getFloat());   break;
            case coInt:     output.set_i(opt.opt->getInt());     break;
            case coString:  output.set_s(static_cast<const ConfigOptionString*>(opt.opt)->value); break;
            case coPercent: output.set_d(opt.opt->getFloat());   break;
            case coPoint:   output.set_s(opt.opt->serialize());  break;
            case coBool:    output.set_b(opt.opt->getBool());    break;
            case coFloatOrPercent:
                boost::throw_exception(qi::expectation_failure<Iterator>(
                    opt.it_range.begin(), opt.it_range.end(), spirit::info("FloatOrPercent variables are not supported")));
            default:
                boost::throw_exception(qi::expectation_failure<Iterator>(
                    opt.it_range.begin(), opt.it_range.end(), spirit::info("Unknown scalar variable type")));
            }
            output.it_range = opt.it_range;
        }

        template <typename Iterator>
        static void vector_variable_reference(
            const MyContext                 *ctx,
            OptWithPos<Iterator>            &opt,
            int                             &index,
            Iterator                         it_end,
            expr<Iterator>                  &output)
        {
            if (opt.opt->is_scalar())
                boost::throw_exception(qi::expectation_failure<Iterator>(
                    opt.it_range.begin(), opt.it_range.end(), spirit::info("Referencing a vector variable in a scalar context")));
            const ConfigOptionVectorBase *vec = static_cast<const ConfigOptionVectorBase*>(opt.opt);
            if (vec->empty())
                boost::throw_exception(qi::expectation_failure<Iterator>(
                    opt.it_range.begin(), opt.it_range.end(), spirit::info("Indexing an empty vector variable")));
            size_t idx = (index < 0) ? 0 : (index >= int(vec->size())) ? 0 : size_t(index);
            switch (opt.opt->type()) {
            case coFloats:   output.set_d(static_cast<const ConfigOptionFloats  *>(opt.opt)->values[idx]); break;
            case coInts:     output.set_i(static_cast<const ConfigOptionInts    *>(opt.opt)->values[idx]); break;
            case coStrings:  output.set_s(static_cast<const ConfigOptionStrings *>(opt.opt)->values[idx]); break;
            case coPercents: output.set_d(static_cast<const ConfigOptionPercents*>(opt.opt)->values[idx]); break;
            case coPoints:   output.set_s(static_cast<const ConfigOptionPoints  *>(opt.opt)->values[idx].dump_perl()); break;
            case coBools:    output.set_b(static_cast<const ConfigOptionBools   *>(opt.opt)->values[idx] != 0); break;
            default:
                boost::throw_exception(qi::expectation_failure<Iterator>(
                    opt.it_range.begin(), opt.it_range.end(), spirit::info("Unknown vector variable type")));
            }
            output.it_range = boost::iterator_range<Iterator>(opt.it_range.begin(), it_end);
        }

        // Verify that the expression returns an integer, which may be used
        // to address a vector.
        template <typename Iterator>
        static void evaluate_index(expr<Iterator> &expr_index, int &output)
        {
            if (expr_index.type != expr<Iterator>::TYPE_INT)
                expr_index.throw_exception("Non-integer index is not allowed to address a vector variable.");
            output = expr_index.i();
        }
    };

    // For debugging the boost::spirit parsers. Print out the string enclosed in it_range.
    template<typename Iterator>
    std::ostream& operator<<(std::ostream& os, const boost::iterator_range<Iterator> &it_range)
    {
        os << std::string(it_range.begin(), it_range.end());
        return os;
    }

    // Disable parsing int numbers (without decimals) and Inf/NaN symbols by the double parser.
    struct strict_real_policies_without_nan_inf : public qi::strict_real_policies<double>
    {
        template <typename It, typename Attr> static bool parse_nan(It&, It const&, Attr&) { return false; }
        template <typename It, typename Attr> static bool parse_inf(It&, It const&, Attr&) { return false; }
    };

    // This parser is to be used inside a raw[] directive to accept a single valid UTF-8 character.
    // If an invalid UTF-8 sequence is encountered, a qi::expectation_failure is thrown.
    struct utf8_char_skipper_parser : qi::primitive_parser<utf8_char_skipper_parser>
    { 
        // Define the attribute type exposed by this parser component 
        template <typename Context, typename Iterator>
        struct attribute
        { 
            typedef wchar_t type;
        };

        // This function is called during the actual parsing process 
        template <typename Iterator, typename Context , typename Skipper, typename Attribute>
        bool parse(Iterator& first, Iterator const& last, Context& context, Skipper const& skipper, Attribute& attr) const 
        { 
            // The skipper shall always be empty, any white space will be accepted.
            // skip_over(first, last, skipper);
            if (first == last)
                return false;
            // Iterator over the UTF-8 sequence.
            auto            it = first;
            // Read the first byte of the UTF-8 sequence.
            unsigned char   c  = static_cast<boost::uint8_t>(*it ++);
            // UTF-8 sequence must not start with a continuation character:
            if ((c & 0xC0) == 0x80)
                goto err;
            // Skip high surrogate first if there is one.
            // If the most significant bit with a zero in it is in position
            // 8-N then there are N bytes in this UTF-8 sequence:
            unsigned int cnt = 0;
            {
                unsigned char mask   = 0x80u;
                unsigned int  result = 0;
                while (c & mask) {
                    ++ result;
                    mask >>= 1;
                }
                cnt = (result == 0) ? 1 : ((result > 4) ? 4 : result);
            }
            // Since we haven't read in a value, we need to validate the code points:
            for (-- cnt; cnt > 0; -- cnt) {
                if (it == last)
                    goto err;
                c = static_cast<boost::uint8_t>(*it ++);
                // We must have a continuation byte:
                if (cnt > 1 && (c & 0xC0) != 0x80)
                    goto err;
            }
            first = it;
            return true;
        err:
            boost::throw_exception(qi::expectation_failure<Iterator>(first, last, spirit::info("Invalid utf8 sequence")));
        }

        // This function is called during error handling to create a human readable string for the error context.
        template <typename Context>
        spirit::info what(Context&) const
        { 
            return spirit::info("unicode_char");
        }
    };

    ///////////////////////////////////////////////////////////////////////////
    //  Our calculator grammar
    ///////////////////////////////////////////////////////////////////////////
    // Inspired by the C grammar rules https://www.lysator.liu.se/c/ANSI-C-grammar-y.html
    template <typename Iterator>
    struct calculator : qi::grammar<Iterator, std::string(const MyContext*), spirit::ascii::space_type>
    {
        calculator() : calculator::base_type(start)
        {
            using namespace qi::labels;
            qi::alpha_type              alpha;
            qi::alnum_type              alnum;
            qi::eps_type                eps;
            qi::raw_type                raw;
            qi::lit_type                lit;
            qi::lexeme_type             lexeme;
            qi::no_skip_type            no_skip;
            qi::real_parser<double, strict_real_policies_without_nan_inf> strict_double;
            spirit::ascii::char_type    char_;
            utf8_char_skipper_parser    utf8char;
            spirit::bool_type           bool_;
            spirit::int_type            int_;
            spirit::double_type         double_;
            spirit::ascii::string_type  string;
            spirit::repository::qi::iter_pos_type iter_pos;
            auto                        kw = spirit::repository::qi::distinct(qi::copy(alnum | '_'));

            qi::_val_type               _val;
            qi::_1_type                 _1;
            qi::_2_type                 _2;
            qi::_a_type                 _a;
            qi::_b_type                 _b;
            qi::_r1_type                _r1;

            // Starting symbol of the grammer.
            // The leading eps is required by the "expectation point" operator ">".
            // Without it, some of the errors would not trigger the error handler.
            start = eps > text_block(_r1);
            start.name("start");

            text_block = *(
                        text [_val+=_1]
                        // Allow back tracking after '{' in case of a text_block embedded inside a condition.
                        // In that case the inner-most {else} wins and the {if}/{elsif}/{else} shall be paired.
                        // {elsif}/{else} without an {if} will be allowed to back track from the embedded text_block.
                    |   (lit('{') >> macro(_r1) [_val+=_1] > '}')
                    |   (lit('[') > legacy_variable_expansion(_r1) [_val+=_1] > ']')
                );
            text_block.name("text_block");

            // Free-form text up to a first brace, including spaces and newlines.
            // The free-form text will be inserted into the processed text without a modification.
            text = no_skip[raw[+(utf8char - char_('[') - char_('{'))]];
            text.name("text");

            // New style of macro expansion.
            // The macro expansion may contain numeric or string expressions, ifs and cases.
            macro =
                    (kw["if"]     > if_else_output(_r1) [_val = _1])
                |   (kw["switch"] > switch_output(_r1)  [_val = _1])
                |   additive_expression(_r1) [ px::bind(&expr<Iterator>::to_string2, _1, _val) ];
            macro.name("macro");

            // An if expression enclosed in {} (the outmost {} are already parsed by the caller).
            if_else_output =
                eps[_b=true] >
                bool_expr_eval(_r1)[_a=_1] > '}' > 
                    text_block(_r1)[px::bind(&expr<Iterator>::set_if, _a, _b, _1, _val)] > '{' >
                *(kw["elsif"] > bool_expr_eval(_r1)[_a=_1] > '}' > 
                    text_block(_r1)[px::bind(&expr<Iterator>::set_if, _a, _b, _1, _val)] > '{') >
                -(kw["else"] > lit('}') > 
                    text_block(_r1)[px::bind(&expr<Iterator>::set_if, _b, _b, _1, _val)] > '{') >
                kw["endif"];
            if_else_output.name("if_else_output");
            // A switch expression enclosed in {} (the outmost {} are already parsed by the caller).
/*
            switch_output =
                eps[_b=true] >
                omit[expr(_r1)[_a=_1]] > '}' > text_block(_r1)[px::bind(&expr<Iterator>::set_if_equal, _a, _b, _1, _val)] > '{' >
                *("elsif" > omit[bool_expr_eval(_r1)[_a=_1]] > '}' > text_block(_r1)[px::bind(&expr<Iterator>::set_if, _a, _b, _1, _val)]) >>
                -("else" > '}' >> text_block(_r1)[px::bind(&expr<Iterator>::set_if, _b, _b, _1, _val)]) >
                "endif";
*/

            // Legacy variable expansion of the original Slic3r, in the form of [scalar_variable] or [vector_variable_index].
            legacy_variable_expansion =
                    (identifier >> &lit(']'))
                        [ px::bind(&MyContext::legacy_variable_expansion<Iterator>, _r1, _1, _val) ]
                |   (identifier > lit('[') > identifier > ']') 
                        [ px::bind(&MyContext::legacy_variable_expansion2<Iterator>, _r1, _1, _2, _val) ]
                ;
            legacy_variable_expansion.name("legacy_variable_expansion");

            identifier =
                ! kw[keywords] >>
                raw[lexeme[(alpha | '_') >> *(alnum | '_')]];
            identifier.name("identifier");

            bool_expr =
                additive_expression(_r1)                   [_val = _1]
                >> *(   ("==" > additive_expression(_r1) ) [px::bind(&expr<Iterator>::equal,     _val, _1)]
                    |   ("!=" > additive_expression(_r1) ) [px::bind(&expr<Iterator>::not_equal, _val, _1)]
                    |   ("<>" > additive_expression(_r1) ) [px::bind(&expr<Iterator>::not_equal, _val, _1)]
                    );
            bool_expr.name("bool expression");

            // Evaluate a boolean expression stored as expr into a boolean value.
            // Throw if the bool_expr does not produce a expr of boolean type.
            bool_expr_eval = bool_expr(_r1) [ px::bind(&expr<Iterator>::evaluate_boolean, _1, _val) ];
            bool_expr_eval.name("bool_expr_eval");

            additive_expression =
                term(_r1)                       [_val  = _1]
                >> *(   (lit('+') > term(_r1) ) [_val += _1]
                    |   (lit('-') > term(_r1) ) [_val -= _1]
                    );
            additive_expression.name("additive_expression");

            term =
                factor(_r1)                       [_val  = _1]
                >> *(   (lit('*') > factor(_r1) ) [_val *= _1]
                    |   (lit('/') > factor(_r1) ) [_val /= _1]
                    );
            term.name("term");

            struct FactorActions {
                static void set_start_pos(Iterator &start_pos, expr<Iterator> &out)
                        { out.it_range = boost::iterator_range<Iterator>(start_pos, start_pos); }
                static void int_(int &value, Iterator &end_pos, expr<Iterator> &out)
                        { out = expr<Iterator>(value, out.it_range.begin(), end_pos); }
                static void double_(double &value, Iterator &end_pos, expr<Iterator> &out)
                        { out = expr<Iterator>(value, out.it_range.begin(), end_pos); }
                static void bool_(bool &value, Iterator &end_pos, expr<Iterator> &out)
                        { out = expr<Iterator>(value, out.it_range.begin(), end_pos); }
                static void string_(boost::iterator_range<Iterator> &it_range, expr<Iterator> &out)
                        { out = expr<Iterator>(std::string(it_range.begin() + 1, it_range.end() - 1), it_range.begin(), it_range.end()); }
                static void expr_(expr<Iterator> &value, Iterator &end_pos, expr<Iterator> &out)
                        { out = expr<Iterator>(std::move(value), out.it_range.begin(), end_pos); }
                static void minus_(expr<Iterator> &value, expr<Iterator> &out)
                        { out = value.unary_minus(out.it_range.begin()); }
                static void not_(expr<Iterator> &value, expr<Iterator> &out)
                        { out = value.unary_not(out.it_range.begin()); }
            };
            factor = iter_pos[px::bind(&FactorActions::set_start_pos, _1, _val)] >> (
                    scalar_variable_reference(_r1)      [ _val = _1 ]
                |   (lit('(')  > additive_expression(_r1) > ')' > iter_pos) [ px::bind(&FactorActions::expr_, _1, _2, _val) ]
                |   (lit('-')  > factor(_r1)           ) [ px::bind(&FactorActions::minus_,  _1,     _val) ]
                |   (lit('+')  > factor(_r1) > iter_pos) [ px::bind(&FactorActions::expr_,   _1, _2, _val) ]
                |   ((kw["not"] | '!') > factor(_r1) > iter_pos) [ px::bind(&FactorActions::not_, _1, _val) ]
                |   (strict_double > iter_pos)           [ px::bind(&FactorActions::double_, _1, _2, _val) ]
                |   (int_      > iter_pos)               [ px::bind(&FactorActions::int_,    _1, _2, _val) ]
                |   (kw[bool_] > iter_pos)               [ px::bind(&FactorActions::bool_,   _1, _2, _val) ]
                |   raw[lexeme['"' > *((utf8char - char_('\\') - char_('"')) | ('\\' > char_)) > '"']]
                                                         [ px::bind(&FactorActions::string_, _1,     _val) ]
                );
            factor.name("factor");

            scalar_variable_reference = 
                variable_reference(_r1)[_a=_1] >>
                (
                        ('[' > additive_expression(_r1)[px::bind(&MyContext::evaluate_index<Iterator>, _1, _b)] > ']' > 
                            iter_pos[px::bind(&MyContext::vector_variable_reference<Iterator>, _r1, _a, _b, _1, _val)])
                    |   eps[px::bind(&MyContext::scalar_variable_reference<Iterator>, _r1, _a, _val)]
                );
            scalar_variable_reference.name("scalar variable reference");

            variable_reference = identifier
                [ px::bind(&MyContext::resolve_variable<Iterator>, _r1, _1, _val) ];
            variable_reference.name("variable reference");
/*
            qi::on_error<qi::fail>(start, 
                    phx::ref(std::cout)
                       << "Error! Expecting "
                       << qi::_4
                       << " here: '"
                       << px::construct<std::string>(qi::_3, qi::_2)
                       << "'\n"
                );
*/

            keywords.add
                ("and")
                ("if")
                //("inf")
                ("else")
                ("elsif")
                ("endif")
                ("false")
                ("not")
                ("or")
                ("true");

            if (0) {
                debug(start);
                debug(text);
                debug(text_block);
                debug(macro);
                debug(if_else_output);
                debug(switch_output);
                debug(legacy_variable_expansion);
                debug(identifier);
                debug(bool_expr);
                debug(bool_expr_eval);
                debug(additive_expression);
                debug(term);
                debug(factor);
                debug(scalar_variable_reference);
                debug(variable_reference);
            }
        }

        // The start of the grammar.
        qi::rule<Iterator, std::string(const MyContext*), spirit::ascii::space_type> start;
        // A free-form text.
        qi::rule<Iterator, std::string(), spirit::ascii::space_type> text;
        // A free-form text, possibly empty, possibly containing macro expansions.
        qi::rule<Iterator, std::string(const MyContext*), spirit::ascii::space_type> text_block;
        // Statements enclosed in curely braces {}
        qi::rule<Iterator, std::string(const MyContext*), spirit::ascii::space_type> macro;
        // Legacy variable expansion of the original Slic3r, in the form of [scalar_variable] or [vector_variable_index].
        qi::rule<Iterator, std::string(const MyContext*), spirit::ascii::space_type> legacy_variable_expansion;
        // Parsed identifier name.
        qi::rule<Iterator,  boost::iterator_range<Iterator>(), spirit::ascii::space_type> identifier;
        // Math expression consisting of +- operators over terms.
        qi::rule<Iterator, expr<Iterator>(const MyContext*), spirit::ascii::space_type> additive_expression;
        // Boolean expressions over expressions.
        qi::rule<Iterator, expr<Iterator>(const MyContext*), spirit::ascii::space_type> bool_expr;
        // Evaluate boolean expression into bool.
        qi::rule<Iterator, bool(const MyContext*), spirit::ascii::space_type> bool_expr_eval;
        // Math expression consisting of */ operators over factors.
        qi::rule<Iterator, expr<Iterator>(const MyContext*), spirit::ascii::space_type> term;
        // Number literals, functions, braced expressions, variable references, variable indexing references.
        qi::rule<Iterator, expr<Iterator>(const MyContext*), spirit::ascii::space_type> factor;
        // Reference of a scalar variable, or reference to a field of a vector variable.
        qi::rule<Iterator, expr<Iterator>(const MyContext*), qi::locals<OptWithPos<Iterator>, int>, spirit::ascii::space_type> scalar_variable_reference;
        // Rule to translate an identifier to a ConfigOption, or to fail.
        qi::rule<Iterator, OptWithPos<Iterator>(const MyContext*), spirit::ascii::space_type> variable_reference;

        qi::rule<Iterator, std::string(const MyContext*), qi::locals<bool, bool>, spirit::ascii::space_type> if_else_output;
        qi::rule<Iterator, std::string(const MyContext*), qi::locals<expr<Iterator>, bool, std::string>, spirit::ascii::space_type> switch_output;

        qi::symbols<char> keywords;
    };
}

struct printer
{
    typedef spirit::utf8_string string;

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

void print_info(spirit::info const& what)
{
    using spirit::basic_info_walker;
    printer pr;
    basic_info_walker<printer> walker(pr, what.tag, 0);
    boost::apply_visitor(walker, what.value);
}

std::string PlaceholderParser::process(const std::string &templ, unsigned int current_extruder_id, const DynamicConfig *config_override) const
{
    typedef std::string::const_iterator iterator_type;
    typedef client::calculator<iterator_type> calculator;

    spirit::ascii::space_type space; // Our skipper
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
    } catch (qi::expectation_failure<iterator_type> const& x) {
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
