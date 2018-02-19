#include "PlaceholderParser.hpp"
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <map>
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

// #define USE_CPP11_REGEX
#ifdef USE_CPP11_REGEX
    #include <regex>
    #define SLIC3R_REGEX_NAMESPACE std
#else /* USE_CPP11_REGEX */
    #include <boost/regex.hpp>
    #define SLIC3R_REGEX_NAMESPACE boost
#endif /* USE_CPP11_REGEX */

namespace Slic3r {

PlaceholderParser::PlaceholderParser()
{
    this->set("version", std::string(SLIC3R_VERSION));
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
            if (this->type == TYPE_STRING) {
                // Convert the right hand side to string and append.
                *this->data.s += rhs.to_string();
            } else if (rhs.type == TYPE_STRING) {
                // Conver the left hand side to string, append rhs.
                this->data.s = new std::string(this->to_string() + rhs.s());
                this->type = TYPE_STRING;
            } else {
                const char *err_msg = "Cannot add non-numeric types.";
                this->throw_if_not_numeric(err_msg);
                rhs.throw_if_not_numeric(err_msg);
                if (this->type == TYPE_DOUBLE || rhs.type == TYPE_DOUBLE) {
                    double d = this->as_d() + rhs.as_d();
                    this->data.d = d;
                    this->type = TYPE_DOUBLE;
                } else
                    this->data.i += rhs.i();
            }
            this->it_range = boost::iterator_range<Iterator>(this->it_range.begin(), rhs.it_range.end());
            return *this;
        }

        expr &operator-=(const expr &rhs)
        { 
            const char *err_msg = "Cannot subtract non-numeric types.";
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

        static void evaluate_boolean_to_string(expr &self, std::string &out)
        {
            if (self.type != TYPE_BOOL)
                self.throw_exception("Not a boolean expression");
            out = self.b() ? "true" : "false";
        }

        // Is lhs==rhs? Store the result into lhs.
        static void compare_op(expr &lhs, expr &rhs, char op, bool invert)
        {
            bool value = false;
            if ((lhs.type == TYPE_INT || lhs.type == TYPE_DOUBLE) &&
                (rhs.type == TYPE_INT || rhs.type == TYPE_DOUBLE)) {
                // Both types are numeric.
                switch (op) {
                    case '=':
                        value = (lhs.type == TYPE_DOUBLE || rhs.type == TYPE_DOUBLE) ? 
                            (std::abs(lhs.as_d() - rhs.as_d()) < 1e-8) : (lhs.i() == rhs.i());
                        break;
                    case '<':
                        value = (lhs.type == TYPE_DOUBLE || rhs.type == TYPE_DOUBLE) ? 
                            (lhs.as_d() < rhs.as_d()) : (lhs.i() < rhs.i());
                        break;
                    case '>':
                    default:
                        value = (lhs.type == TYPE_DOUBLE || rhs.type == TYPE_DOUBLE) ? 
                            (lhs.as_d() > rhs.as_d()) : (lhs.i() > rhs.i());
                        break;
                }
            } else if (lhs.type == TYPE_BOOL && rhs.type == TYPE_BOOL) {
                // Both type are bool.
                if (op != '=')
                    boost::throw_exception(qi::expectation_failure<Iterator>(
                        lhs.it_range.begin(), rhs.it_range.end(), spirit::info("*Cannot compare the types.")));
                value = lhs.b() == rhs.b();
            } else if (lhs.type == TYPE_STRING || rhs.type == TYPE_STRING) {
                // One type is string, the other could be converted to string.
                value = (op == '=') ? (lhs.to_string() == rhs.to_string()) : 
                        (op == '<') ? (lhs.to_string() < rhs.to_string()) : (lhs.to_string() > rhs.to_string());
            } else {
                boost::throw_exception(qi::expectation_failure<Iterator>(
                    lhs.it_range.begin(), rhs.it_range.end(), spirit::info("*Cannot compare the types.")));
            }
            lhs.type = TYPE_BOOL;
            lhs.data.b = invert ? ! value : value;
        }
        static void equal    (expr &lhs, expr &rhs) { compare_op(lhs, rhs, '=', false); }
        static void not_equal(expr &lhs, expr &rhs) { compare_op(lhs, rhs, '=', true ); }
        static void lower    (expr &lhs, expr &rhs) { compare_op(lhs, rhs, '<', false); }
        static void greater  (expr &lhs, expr &rhs) { compare_op(lhs, rhs, '>', false); }
        static void leq      (expr &lhs, expr &rhs) { compare_op(lhs, rhs, '>', true ); }
        static void geq      (expr &lhs, expr &rhs) { compare_op(lhs, rhs, '<', true ); }

        static void regex_op(expr &lhs, boost::iterator_range<Iterator> &rhs, char op)
        {
            const std::string *subject  = nullptr;
            const std::string *mask     = nullptr;
            if (lhs.type == TYPE_STRING) {
                // One type is string, the other could be converted to string.
                subject = &lhs.s();
            } else {
                lhs.throw_exception("Left hand side of a regex match must be a string.");
            }
            try {
                std::string pattern(++ rhs.begin(), -- rhs.end());
                bool result = SLIC3R_REGEX_NAMESPACE::regex_match(*subject, SLIC3R_REGEX_NAMESPACE::regex(pattern));
                if (op == '!')
                    result = ! result;
                lhs.reset();
                lhs.type = TYPE_BOOL;
                lhs.data.b = result;
            } catch (SLIC3R_REGEX_NAMESPACE::regex_error &ex) {
                // Syntax error in the regular expression
                boost::throw_exception(qi::expectation_failure<Iterator>(
                    rhs.begin(), rhs.end(), spirit::info(std::string("*Regular expression compilation failed: ") + ex.what())));
            }
        }

        static void regex_matches     (expr &lhs, boost::iterator_range<Iterator> &rhs) { return regex_op(lhs, rhs, '='); }
        static void regex_doesnt_match(expr &lhs, boost::iterator_range<Iterator> &rhs) { return regex_op(lhs, rhs, '!'); }

        static void logical_op(expr &lhs, expr &rhs, char op)
        {
            bool value = false;
            if (lhs.type == TYPE_BOOL && rhs.type == TYPE_BOOL) {
                value = (op == '|') ? (lhs.b() || rhs.b()) : (lhs.b() && rhs.b());
            } else {
                boost::throw_exception(qi::expectation_failure<Iterator>(
                    lhs.it_range.begin(), rhs.it_range.end(), spirit::info("*Cannot apply logical operation to non-boolean operators.")));
            }
            lhs.type   = TYPE_BOOL;
            lhs.data.b = value;
        }
        static void logical_or (expr &lhs, expr &rhs) { logical_op(lhs, rhs, '|'); }
        static void logical_and(expr &lhs, expr &rhs) { logical_op(lhs, rhs, '&'); }

        static void ternary_op(expr &lhs, expr &rhs1, expr &rhs2)
        {
            bool value = false;
            if (lhs.type != TYPE_BOOL)
                lhs.throw_exception("Not a boolean expression");
            if (lhs.b())
                lhs = std::move(rhs1);
            else
                lhs = std::move(rhs2);
        }

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
                this->it_range.begin(), this->it_range.end(), spirit::info(std::string("*") + message)));
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
        const DynamicConfig     *config                 = nullptr;
        const DynamicConfig     *config_override        = nullptr;
        size_t                   current_extruder_id    = 0;
        // If false, the macro_processor will evaluate a full macro.
        // If true, the macro processor will evaluate just a boolean condition using the full expressive power of the macro processor.
        bool                     just_boolean_expression = false;
        std::string              error_message;

        // Table to translate symbol tag to a human readable error message.
        static std::map<std::string, std::string> tag_to_error_message;

        static void             evaluate_full_macro(const MyContext *ctx, bool &result) { result = ! ctx->just_boolean_expression; }

        const ConfigOption*     resolve_symbol(const std::string &opt_key) const
        {
            const ConfigOption *opt = nullptr;
            if (config_override != nullptr)
                opt = config_override->option(opt_key);
            if (opt == nullptr)
                opt = config->option(opt_key);
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
                            ctx->throw_exception("Trying to index a scalar variable", opt_key);
                        char *endptr = nullptr;
                        idx = strtol(opt_key_str.c_str() + idx + 1, &endptr, 10);
                        if (endptr == nullptr || *endptr != 0)
                            ctx->throw_exception("Invalid vector index", boost::iterator_range<Iterator>(opt_key.begin() + idx + 1, opt_key.end()));
                    }
                }
            }
            if (opt == nullptr)
                ctx->throw_exception("Variable does not exist", boost::iterator_range<Iterator>(opt_key.begin(), opt_key.end()));
            if (opt->is_scalar())
                output = opt->serialize();
            else {
                const ConfigOptionVectorBase *vec = static_cast<const ConfigOptionVectorBase*>(opt);
                if (vec->empty())
                    ctx->throw_exception("Indexing an empty vector variable", opt_key);
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
                ctx->throw_exception("Trying to index a scalar variable", opt_key);
            const ConfigOptionVectorBase *vec = static_cast<const ConfigOptionVectorBase*>(opt);
            if (vec->empty())
                ctx->throw_exception("Indexing an empty vector variable", boost::iterator_range<Iterator>(opt_key.begin(), opt_key.end()));
            const ConfigOption *opt_index = ctx->resolve_symbol(std::string(opt_vector_index.begin(), opt_vector_index.end()));
            if (opt_index == nullptr)
                ctx->throw_exception("Variable does not exist", opt_key);
            if (opt_index->type() != coInt)
                ctx->throw_exception("Indexing variable has to be integer", opt_key);
			int idx = opt_index->getInt();
			if (idx < 0)
                ctx->throw_exception("Negative vector index", opt_key);
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
                ctx->throw_exception("Not a variable name", opt_key);
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
                ctx->throw_exception("Referencing a vector variable when scalar is expected", opt.it_range);
            switch (opt.opt->type()) {
            case coFloat:   output.set_d(opt.opt->getFloat());   break;
            case coInt:     output.set_i(opt.opt->getInt());     break;
            case coString:  output.set_s(static_cast<const ConfigOptionString*>(opt.opt)->value); break;
            case coPercent: output.set_d(opt.opt->getFloat());   break;
            case coPoint:   output.set_s(opt.opt->serialize());  break;
            case coBool:    output.set_b(opt.opt->getBool());    break;
            case coFloatOrPercent:
                ctx->throw_exception("FloatOrPercent variables are not supported", opt.it_range);
            default:
                ctx->throw_exception("Unknown scalar variable type", opt.it_range);
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
                ctx->throw_exception("Referencing a scalar variable when vector is expected", opt.it_range);
            const ConfigOptionVectorBase *vec = static_cast<const ConfigOptionVectorBase*>(opt.opt);
            if (vec->empty())
                ctx->throw_exception("Indexing an empty vector variable", opt.it_range);
            size_t idx = (index < 0) ? 0 : (index >= int(vec->size())) ? 0 : size_t(index);
            switch (opt.opt->type()) {
            case coFloats:   output.set_d(static_cast<const ConfigOptionFloats  *>(opt.opt)->values[idx]); break;
            case coInts:     output.set_i(static_cast<const ConfigOptionInts    *>(opt.opt)->values[idx]); break;
            case coStrings:  output.set_s(static_cast<const ConfigOptionStrings *>(opt.opt)->values[idx]); break;
            case coPercents: output.set_d(static_cast<const ConfigOptionPercents*>(opt.opt)->values[idx]); break;
            case coPoints:   output.set_s(static_cast<const ConfigOptionPoints  *>(opt.opt)->values[idx].dump_perl()); break;
            case coBools:    output.set_b(static_cast<const ConfigOptionBools   *>(opt.opt)->values[idx] != 0); break;
            default:
                ctx->throw_exception("Unknown vector variable type", opt.it_range);
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

        template <typename Iterator>
        static void throw_exception(const std::string &msg, const boost::iterator_range<Iterator> &it_range)
        {
            // An asterix is added to the start of the string to differentiate the boost::spirit::info::tag content
            // between the grammer terminal / non-terminal symbol name and a free-form error message.
            boost::throw_exception(qi::expectation_failure<Iterator>(it_range.begin(), it_range.end(), spirit::info(std::string("*") + msg)));
        }

        template <typename Iterator>
        static void process_error_message(const MyContext *context, const boost::spirit::info &info, const Iterator &it_begin, const Iterator &it_end, const Iterator &it_error)
        {
            std::string &msg = const_cast<MyContext*>(context)->error_message;
            std::string  first(it_begin, it_error);
            std::string  last(it_error, it_end);
            auto         first_pos  = first.rfind('\n');
            auto         last_pos   = last.find('\n');
            int          line_nr    = 1;
            if (first_pos == std::string::npos)
                first_pos = 0;
            else {
                // Calculate the current line number.
                for (size_t i = 0; i <= first_pos; ++ i)
                    if (first[i] == '\n')
                        ++ line_nr;
                ++ first_pos;
            }
            auto error_line = std::string(first, first_pos) + std::string(last, 0, last_pos);
            // Position of the it_error from the start of its line.
            auto error_pos  = (it_error - it_begin) - first_pos;
            msg += "Parsing error at line " + std::to_string(line_nr);
            if (! info.tag.empty() && info.tag.front() == '*') {
                // The gat contains an explanatory string.
                msg += ": ";
                msg += info.tag.substr(1);
            } else {
                auto it = tag_to_error_message.find(info.tag);
                if (it == tag_to_error_message.end()) {
                    // A generic error report based on the nonterminal or terminal symbol name.
                    msg += ". Expecting tag ";
                    msg += info.tag;
                } else {
                    // Use the human readable error message.
                    msg += ". ";
                    msg + it->second;
                }
            }
            msg += '\n';
            msg += error_line;
            msg += '\n';
            for (size_t i = 0; i < error_pos; ++ i)
                msg += ' ';
            msg += "^\n";
        }
    };

    // Table to translate symbol tag to a human readable error message.
    std::map<std::string, std::string> MyContext::tag_to_error_message = {
        { "eoi",                        "Unknown syntax error" },
        { "start",                      "Unknown syntax error" },
        { "text",                       "Invalid text." },
        { "text_block",                 "Invalid text block." },
        { "macro",                      "Invalid macro." },
        { "if_else_output",             "Not an {if}{else}{endif} macro." },
        { "switch_output",              "Not a {switch} macro." },
        { "legacy_variable_expansion",  "Expecting a legacy variable expansion format" },
        { "identifier",                 "Expecting an identifier." },
        { "conditional_expression",     "Expecting a conditional expression." },
        { "logical_or_expression",      "Expecting a boolean expression." },
        { "logical_and_expression",     "Expecting a boolean expression." },
        { "equality_expression",        "Expecting an expression." },
        { "bool_expr_eval",             "Expecting a boolean expression."},
        { "relational_expression",      "Expecting an expression." },
        { "additive_expression",        "Expecting an expression." },
        { "multiplicative_expression",  "Expecting an expression." },
        { "unary_expression",           "Expecting an expression." },
        { "scalar_variable_reference",  "Expecting a scalar variable reference."},
        { "variable_reference",         "Expecting a variable reference."},
        { "regular_expression",         "Expecting a regular expression."}
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
            unsigned int    cnt = 0;
            // UTF-8 sequence must not start with a continuation character:
            if ((c & 0xC0) == 0x80)
                goto err;
            // Skip high surrogate first if there is one.
            // If the most significant bit with a zero in it is in position
            // 8-N then there are N bytes in this UTF-8 sequence:
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
            MyContext::throw_exception("Invalid utf8 sequence", boost::iterator_range<Iterator>(first, last));
            return false;
        }

        // This function is called during error handling to create a human readable string for the error context.
        template <typename Context>
        spirit::info what(Context&) const
        { 
            return spirit::info("unicode_char");
        }
    };

    ///////////////////////////////////////////////////////////////////////////
    //  Our macro_processor grammar
    ///////////////////////////////////////////////////////////////////////////
    // Inspired by the C grammar rules https://www.lysator.liu.se/c/ANSI-C-grammar-y.html
    template <typename Iterator>
    struct macro_processor : qi::grammar<Iterator, std::string(const MyContext*), qi::locals<bool>, spirit::ascii::space_type>
    {
        macro_processor() : macro_processor::base_type(start)
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
			spirit::eoi_type			eoi;
			spirit::repository::qi::iter_pos_type iter_pos;
            auto                        kw = spirit::repository::qi::distinct(qi::copy(alnum | '_'));

            qi::_val_type               _val;
            qi::_1_type                 _1;
            qi::_2_type                 _2;
            qi::_3_type                 _3;
            qi::_4_type                 _4;
            qi::_a_type                 _a;
            qi::_b_type                 _b;
            qi::_r1_type                _r1;

            // Starting symbol of the grammer.
            // The leading eps is required by the "expectation point" operator ">".
            // Without it, some of the errors would not trigger the error handler.
            // Also the start symbol switches between the "full macro syntax" and a "boolean expression only",
            // depending on the context->just_boolean_expression flag. This way a single static expression parser
            // could serve both purposes.
            start = eps[px::bind(&MyContext::evaluate_full_macro, _r1, _a)] >
                (       eps(_a==true) > text_block(_r1) [_val=_1]
                    |   conditional_expression(_r1) [ px::bind(&expr<Iterator>::evaluate_boolean_to_string, _1, _val) ]
				) > eoi;
            start.name("start");
            qi::on_error<qi::fail>(start, px::bind(&MyContext::process_error_message<Iterator>, _r1, _4, _1, _2, _3));

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
//                |   (kw["switch"] > switch_output(_r1)  [_val = _1])
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

            conditional_expression =
                logical_or_expression(_r1)                [_val = _1]
                >> -('?' > conditional_expression(_r1) > ':' > conditional_expression(_r1)) [px::bind(&expr<Iterator>::ternary_op, _val, _1, _2)];
            conditional_expression.name("conditional_expression");

            logical_or_expression = 
                logical_and_expression(_r1)                [_val = _1]
                >> *(   ((kw["or"] | "||") > logical_and_expression(_r1) ) [px::bind(&expr<Iterator>::logical_or, _val, _1)] );
            logical_or_expression.name("logical_or_expression");

            logical_and_expression = 
                equality_expression(_r1)                   [_val = _1]
                >> *(   ((kw["and"] | "&&") > equality_expression(_r1) ) [px::bind(&expr<Iterator>::logical_and, _val, _1)] );
            logical_and_expression.name("logical_and_expression");

            equality_expression =
                relational_expression(_r1)                   [_val = _1]
                >> *(   ("==" > relational_expression(_r1) ) [px::bind(&expr<Iterator>::equal,     _val, _1)]
                    |   ("!=" > relational_expression(_r1) ) [px::bind(&expr<Iterator>::not_equal, _val, _1)]
                    |   ("<>" > relational_expression(_r1) ) [px::bind(&expr<Iterator>::not_equal, _val, _1)]
                    |   ("=~" > regular_expression         ) [px::bind(&expr<Iterator>::regex_matches, _val, _1)]
                    |   ("!~" > regular_expression         ) [px::bind(&expr<Iterator>::regex_doesnt_match, _val, _1)]
                    );
            equality_expression.name("bool expression");

            // Evaluate a boolean expression stored as expr into a boolean value.
            // Throw if the equality_expression does not produce a expr of boolean type.
            bool_expr_eval = conditional_expression(_r1) [ px::bind(&expr<Iterator>::evaluate_boolean, _1, _val) ];
            bool_expr_eval.name("bool_expr_eval");

            relational_expression = 
                    additive_expression(_r1)                [_val  = _1]
                >> *(   ("<="     > additive_expression(_r1) ) [px::bind(&expr<Iterator>::leq,     _val, _1)]
                    |   (">="     > additive_expression(_r1) ) [px::bind(&expr<Iterator>::geq,     _val, _1)]
                    |   (lit('<') > additive_expression(_r1) ) [px::bind(&expr<Iterator>::lower,   _val, _1)]
                    |   (lit('>') > additive_expression(_r1) ) [px::bind(&expr<Iterator>::greater, _val, _1)]
                    );
            relational_expression.name("relational_expression");

            additive_expression =
                multiplicative_expression(_r1)                       [_val  = _1]
                >> *(   (lit('+') > multiplicative_expression(_r1) ) [_val += _1]
                    |   (lit('-') > multiplicative_expression(_r1) ) [_val -= _1]
                    );
            additive_expression.name("additive_expression");

            multiplicative_expression =
                unary_expression(_r1)                       [_val  = _1]
                >> *(   (lit('*') > unary_expression(_r1) ) [_val *= _1]
                    |   (lit('/') > unary_expression(_r1) ) [_val /= _1]
                    );
            multiplicative_expression.name("multiplicative_expression");

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
            unary_expression = iter_pos[px::bind(&FactorActions::set_start_pos, _1, _val)] >> (
                    scalar_variable_reference(_r1)                  [ _val = _1 ]
                |   (lit('(')  > conditional_expression(_r1) > ')' > iter_pos) [ px::bind(&FactorActions::expr_, _1, _2, _val) ]
                |   (lit('-')  > unary_expression(_r1)           )  [ px::bind(&FactorActions::minus_,  _1,     _val) ]
                |   (lit('+')  > unary_expression(_r1) > iter_pos)  [ px::bind(&FactorActions::expr_,   _1, _2, _val) ]
                |   ((kw["not"] | '!') > unary_expression(_r1) > iter_pos) [ px::bind(&FactorActions::not_, _1, _val) ]
                |   (strict_double > iter_pos)                      [ px::bind(&FactorActions::double_, _1, _2, _val) ]
                |   (int_      > iter_pos)                          [ px::bind(&FactorActions::int_,    _1, _2, _val) ]
                |   (kw[bool_] > iter_pos)                          [ px::bind(&FactorActions::bool_,   _1, _2, _val) ]
                |   raw[lexeme['"' > *((utf8char - char_('\\') - char_('"')) | ('\\' > char_)) > '"']]
                                                                    [ px::bind(&FactorActions::string_, _1,     _val) ]
                );
            unary_expression.name("unary_expression");

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

            regular_expression = raw[lexeme['/' > *((utf8char - char_('\\') - char_('/')) | ('\\' > char_)) > '/']];
            regular_expression.name("regular_expression");

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
//                debug(switch_output);
                debug(legacy_variable_expansion);
                debug(identifier);
                debug(conditional_expression);
                debug(logical_or_expression);
                debug(logical_and_expression);
                debug(equality_expression);
                debug(bool_expr_eval);
                debug(relational_expression);
                debug(additive_expression);
                debug(multiplicative_expression);
                debug(unary_expression);
                debug(scalar_variable_reference);
                debug(variable_reference);
                debug(regular_expression);
            }
        }

        // Generic expression over expr<Iterator>.
        typedef qi::rule<Iterator, expr<Iterator>(const MyContext*), spirit::ascii::space_type> RuleExpression;

        // The start of the grammar.
        qi::rule<Iterator, std::string(const MyContext*), qi::locals<bool>, spirit::ascii::space_type> start;
        // A free-form text.
        qi::rule<Iterator, std::string(), spirit::ascii::space_type> text;
        // A free-form text, possibly empty, possibly containing macro expansions.
        qi::rule<Iterator, std::string(const MyContext*), spirit::ascii::space_type> text_block;
        // Statements enclosed in curely braces {}
        qi::rule<Iterator, std::string(const MyContext*), spirit::ascii::space_type> macro;
        // Legacy variable expansion of the original Slic3r, in the form of [scalar_variable] or [vector_variable_index].
        qi::rule<Iterator, std::string(const MyContext*), spirit::ascii::space_type> legacy_variable_expansion;
        // Parsed identifier name.
        qi::rule<Iterator, boost::iterator_range<Iterator>(), spirit::ascii::space_type> identifier;
        // Ternary operator (?:) over logical_or_expression.
        RuleExpression conditional_expression;
        // Logical or over logical_and_expressions.
        RuleExpression logical_or_expression;
        // Logical and over relational_expressions.
        RuleExpression logical_and_expression;
        // <, >, <=, >=
        RuleExpression relational_expression;
        // Math expression consisting of +- operators over multiplicative_expressions.
        RuleExpression additive_expression;
        // Boolean expressions over expressions.
        RuleExpression equality_expression;
        // Math expression consisting of */ operators over factors.
        RuleExpression multiplicative_expression;
        // Number literals, functions, braced expressions, variable references, variable indexing references.
        RuleExpression unary_expression;
        // Rule to capture a regular expression enclosed in //.
        qi::rule<Iterator, boost::iterator_range<Iterator>(), spirit::ascii::space_type> regular_expression;
        // Evaluate boolean expression into bool.
        qi::rule<Iterator, bool(const MyContext*), spirit::ascii::space_type> bool_expr_eval;
        // Reference of a scalar variable, or reference to a field of a vector variable.
        qi::rule<Iterator, expr<Iterator>(const MyContext*), qi::locals<OptWithPos<Iterator>, int>, spirit::ascii::space_type> scalar_variable_reference;
        // Rule to translate an identifier to a ConfigOption, or to fail.
        qi::rule<Iterator, OptWithPos<Iterator>(const MyContext*), spirit::ascii::space_type> variable_reference;

        qi::rule<Iterator, std::string(const MyContext*), qi::locals<bool, bool>, spirit::ascii::space_type> if_else_output;
//        qi::rule<Iterator, std::string(const MyContext*), qi::locals<expr<Iterator>, bool, std::string>, spirit::ascii::space_type> switch_output;

        qi::symbols<char> keywords;
    };
}

static std::string process_macro(const std::string &templ, client::MyContext &context)
{
    typedef std::string::const_iterator iterator_type;
    typedef client::macro_processor<iterator_type> macro_processor;

    // Our whitespace skipper.
    spirit::ascii::space_type   space;
    // Our grammar, statically allocated inside the method, meaning it will be allocated the first time
    // PlaceholderParser::process() runs.
    //FIXME this kind of initialization is not thread safe!
    static macro_processor      macro_processor_instance;
    // Iterators over the source template.
    std::string::const_iterator iter = templ.begin();
    std::string::const_iterator end  = templ.end();
    // Accumulator for the processed template.
    std::string                 output;
    bool res = phrase_parse(iter, end, macro_processor_instance(&context), space, output);
	if (!context.error_message.empty()) {
        if (context.error_message.back() != '\n' && context.error_message.back() != '\r')
            context.error_message += '\n';
        throw std::runtime_error(context.error_message);
    }
    return output;
}

std::string PlaceholderParser::process(const std::string &templ, unsigned int current_extruder_id, const DynamicConfig *config_override) const
{
    client::MyContext context;
    context.config              = &this->config();
    context.config_override     = config_override;
    context.current_extruder_id = current_extruder_id;
    return process_macro(templ, context);
}

// Evaluate a boolean expression using the full expressive power of the PlaceholderParser boolean expression syntax.
// Throws std::runtime_error on syntax or runtime error.
bool PlaceholderParser::evaluate_boolean_expression(const std::string &templ, const DynamicConfig &config, const DynamicConfig *config_override)
{
    client::MyContext context;
    context.config                  = &config;
    context.config_override         = config_override;
    // Let the macro processor parse just a boolean expression, not the full macro language.
    context.just_boolean_expression = true;
    return process_macro(templ, context) == "true";
}

}
