#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <limits>
#include <variant>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <memory>

#include <unordered_map>

template <typename T>
T vec_pop_back(std::vector<T> & v)
{
    T ret = std::move(v.back());
    v.pop_back();
    return ret;
}

using namespace std;

typedef uint32_t iword_t;
typedef  int32_t iwordsigned_t;

// token kind
enum TKind : iword_t {
    GlobalVar,
    GlobalVarDec,
    GlobalVarLookup,
    GlobalVarDecLookup,
    
    LocalVar,
    LocalVarDec,
    LocalVarLookup,
    LocalVarDecLookup,
    
    FuncDec,
    FuncLookup,
    FuncEnd,
    
    LabelDec,
    LabelLookup,
    
    Integer,
    IntegerInline,
    IntegerInlineBigDec,
    IntegerInlineBigBin,
    Double,
    DoubleInline,
    
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    
    AddAssign,
    SubAssign,
    MulAssign,
    DivAssign,
    ModAssign,
    
    And,
    Or,
    Xor,
    
    Shl,
    Shr,
    
    BoolAnd,
    BoolOr,
    
    Assign,
    AssignVar,
    Return,
    Call,
    CallEval,
    
    IfGoto,
    IfGotoLabel,
    
    IfGotoLabelEQ,
    IfGotoLabelNE,
    IfGotoLabelLE,
    IfGotoLabelGE,
    IfGotoLabelLT,
    IfGotoLabelGT,
    
    Goto,
    GotoLabel,
    
    ForLoop,
    ForLoopLabel,
    ForLoopFull,
    
    ScopeOpen,
    ScopeClose,
    
    CmpEQ,
    CmpNE,
    CmpLE,
    CmpGE,
    CmpLT,
    CmpGT,
    
    ArrayBuild,
    ArrayIndex,
    Clone,
    //CloneDeep,
    
    ArrayLen,
    ArrayLenMinusOne,
    ArrayPushIn,
    ArrayPopOut,
    
    StringLiteral,
    StringLitReference,
    
    BuiltinCall,
    
    Exit,
};

#define TOKEN_LOG(NAME, TYPE)\
vector<TYPE> token_##NAME##s;\
iword_t get_token_##NAME##_num(TYPE s)\
{\
    auto n = token_##NAME##s.size();\
    for (iword_t i = 0; i < n; i++)\
    {\
        if (token_##NAME##s[i] == s)\
            return i;\
    }\
    token_##NAME##s.push_back(s);\
    return n;\
}\
TYPE get_token_##NAME(iword_t n)\
{\
    return token_##NAME##s[n];\
}

TOKEN_LOG(string, string)
TOKEN_LOG(func, string)
TOKEN_LOG(varname, string)
TOKEN_LOG(int, int64_t)
TOKEN_LOG(double, double)

struct Token {
    TKind kind;
    iword_t n;
    iword_t extra_1;
    iword_t extra_2;
};

Token make_token(TKind kind, iword_t n)
{
    return {kind, n, 0, 0};
}

struct DynamicType;
struct Ref { DynamicType * ref; };

struct Label { int loc; };
struct Array { shared_ptr<vector<DynamicType>> items; };
struct Func {
    iword_t loc;
    iword_t len;
    iword_t name;
};


// DynamicType can hold any of these types
struct DynamicType {
    std::variant<int64_t, double, Ref, Label, Func, Array> value;

    DynamicType() : value((int64_t)0) {}
    DynamicType(int64_t v) : value(v) {}
    DynamicType(int v) : value((int64_t)v) {}
    DynamicType(short v) : value((int64_t)v) {}
    DynamicType(char v) : value((int64_t)v) {}
    DynamicType(double v) : value(v) {}
    DynamicType(const Ref& v) : value(v) {}
    DynamicType(const Label& l) : value(l) {}
    DynamicType(const Func& f) : value(f) {}
    DynamicType(const Array& a) : value(a) {}

    DynamicType(const DynamicType& other) = default;
    DynamicType(DynamicType&& other) noexcept = default;
    DynamicType& operator=(const DynamicType& other) = default;
    DynamicType& operator=(DynamicType&& other) noexcept = default;
    
    #define INFIX(WRAPPER1, WRAPPER2, OP, OP2, WX)\
        if (std::holds_alternative<int64_t>(value) && std::holds_alternative<int64_t>(other.value))\
            return WRAPPER1(WX(std::get<int64_t>(value)) OP WX(std::get<int64_t>(other.value)));\
        else if (std::holds_alternative<double>(value) && std::holds_alternative<double>(other.value))\
            return WRAPPER2(WX(std::get<double>(value)) OP2 WX(std::get<double>(other.value)));\
        else if (std::holds_alternative<int64_t>(value) && std::holds_alternative<double>(other.value))\
            return WRAPPER2(WX(std::get<int64_t>(value)) OP2 WX(std::get<double>(other.value)));\
        else if (std::holds_alternative<double>(value) && std::holds_alternative<int64_t>(other.value))\
            return WRAPPER2(WX(std::get<double>(value)) OP2 WX(std::get<int64_t>(other.value)));\
        throw std::runtime_error("Unsupported operation: non-numeric operands");
    
    #define COMMA ,
    
    DynamicType operator+(const DynamicType& other) const { INFIX(DynamicType, DynamicType, +, +, ) }
    DynamicType operator-(const DynamicType& other) const { INFIX(DynamicType, DynamicType, -, -, ) }
    DynamicType operator*(const DynamicType& other) const { INFIX(DynamicType, DynamicType, *, *, ) }
    DynamicType operator/(const DynamicType& other) const { INFIX(DynamicType, DynamicType, /, /, ) }
    DynamicType operator%(const DynamicType& other) const { INFIX(DynamicType, fmod, %, COMMA, ) }
    
    bool operator>=(const DynamicType& other) const { INFIX(!!, !!, >=, >=, ) }
    bool operator<=(const DynamicType& other) const { INFIX(!!, !!, <=, <=, ) }
    bool operator==(const DynamicType& other) const { INFIX(!!, !!, ==, ==, ) }
    bool operator!=(const DynamicType& other) const { INFIX(!!, !!, !=, !=, ) }
    bool operator>(const DynamicType& other) const { INFIX(!!, !!, >, >, ) }
    bool operator<(const DynamicType& other) const { INFIX(!!, !!, <, <, ) }
    
    DynamicType operator<<(const DynamicType& other) const { INFIX(int64_t, int64_t, <<, <<, int64_t) }
    DynamicType operator>>(const DynamicType& other) const { INFIX(int64_t, int64_t, >>, >>, int64_t) }
    
    DynamicType operator&(const DynamicType& other) const { INFIX(int64_t, int64_t, &, &, uint64_t) }
    DynamicType operator|(const DynamicType& other) const { INFIX(int64_t, int64_t, |, |, uint64_t) }
    DynamicType operator^(const DynamicType& other) const { INFIX(int64_t, int64_t, ^, ^, uint64_t) }
    
    bool operator&&(const DynamicType& other) const { INFIX(!!, !!, &&, &&, ) }
    bool operator||(const DynamicType& other) const { INFIX(!!, !!, ||, ||, ) }
    
    #define AS_TYPE_X(TYPE, TYPENAME)\
    TYPE & as_##TYPENAME()\
    {\
        if (std::holds_alternative<TYPE>(value))\
            return std::get<TYPE>(value);\
        throw std::runtime_error("Value is not of type " #TYPENAME);\
    }
    
    AS_TYPE_X(int64_t, int)
    AS_TYPE_X(double, double)
    AS_TYPE_X(Ref, ref)
    AS_TYPE_X(Label, label)
    AS_TYPE_X(Func, func)
    AS_TYPE_X(Array, array)
    
    bool is_int() { return std::holds_alternative<int64_t>(value); }
    bool is_double() { return std::holds_alternative<double>(value); }
    bool is_ref() { return std::holds_alternative<Ref>(value); }
    bool is_label() { return std::holds_alternative<Label>(value); }
    bool is_func() { return std::holds_alternative<Func>(value); }
    bool is_array() { return std::holds_alternative<Array>(value); }
    
    int64_t as_into_int()
    {
        if (is_int()) return as_int();
        if (is_double()) return as_double();
        throw std::runtime_error("Value is not of a numeric type");
    }
    
    explicit operator bool() const
    {
        if (std::holds_alternative<int64_t>(value))
            return !!std::get<int64_t>(value);
        else if (std::holds_alternative<double>(value))
            return !!std::get<double>(value);
        return true;
    }
};

TOKEN_LOG(stringval, vector<DynamicType>)
TOKEN_LOG(stringref, shared_ptr<vector<DynamicType>>)

// built-in function definitions. must be specifically here. do not move.
#include "builtins.hpp"

struct Program {
    vector<Token> program;
    vector<int> lines;
    vector<Func> funcs;
};

Program load_program(string text)
{
    size_t line = 0;
    size_t i = 0, i2 = 0;

    vector<string> program_texts;
    vector<int> lines;
    
    while (i < text.size())
    {
        while (i < text.size() && isspace(text[i]))
        {
            if (text[i] == '\n')
                line++;
            i++;
        }

        if (i < text.size() && text[i] == '#')
        {
            while (i < text.size() && text[i] != '\n') i++;
        }
        else
        {
            i2 = i;
            if (text[i2] == '"')
            {
                i2++;
                string s = "\"";
                while (i2 < text.size())
                {
                    if (text[i2] == '\\' && i2 + 1 < text.size())
                    {
                        if (text[i2+1] == 'x' && i2 + 3 < text.size())
                        {
                            char str[3] = {text[i2+2], text[i2+3], 0};
                            char c = std::strtol(str, nullptr, 16);
                            s += c;
                        }
                        else if (text[i2+1] == 'n') s += '\n';
                        else if (text[i2+1] == 'r') s += '\r';
                        else if (text[i2+1] == 't') s += '\t';
                        else if (text[i2+1] == '\\') s += '\\';
                        else if (text[i2+1] == '\0') s += '\0';
                        else
                            s += text[i2+1];
                        i2 += 1;
                    }
                    else
                        s += text[i2];
                    if (s.back() == '"')
                        break;
                    i2 += 1;
                }
                i2 += 1;
                
                if (text[i2] == '&') // reference-type strings
                    s += text[i2++];
                
                if (!isspace(text[i2]) && text[i2] != 0)
                    throw runtime_error("String literals must not have trailing text after the closing \" character");
            }
            else
            {
                while (i2 < text.size() && !isspace(text[i2]))
                    i2++;
            }
            program_texts.push_back(text.substr(i, i2 - i));
            lines.push_back(line + 1);
            i = i2;
        }
    }
    
    lines.push_back(lines.back());
    
    //for (auto & n : program_texts)
    //    printf("%s\n", n.data());
    
    //for (auto & n : lines)
    //    printf("%d\n", n);
    //printf("(%d)\n", lines.size());
    
    auto isint = [&](const std::string& str) {
        if (str.empty()) return false;
        
        for (size_t i = (str[0] == '+' || str[0] == '-'); i < str.length(); i++)
        {
            if (!std::isdigit(str[i]))
                return false;
        }
        
        return true;
    };
    auto isfloat = [&](const string& str) {
        if (isint(str)) return false;
        try
        {
            stod(str);
            return true;
        }
        catch (...)
        {
            return false;
        }
    };
    
    auto isname = [&](const string& str) {
        if (str.empty() || (!isalpha(str[0]) && str[0] != '_'))
            return false;
        for (size_t i = 1; i < str.size(); i++)
        {
            if (!isalnum(str[i]) && str[i] != '_')
                return false;
        }
        return true;
    };
    
    get_token_func_num("");
    
    vector<Token> program;
    
    vector<vector<string>> var_defs;
    var_defs.push_back({});
    
    auto var_is_local = [&](string & s) {
        if (var_defs.size() == 1)
            return false;
        for (size_t i = 0; i < var_defs.back().size(); i++)
        {
            if (var_defs.back()[i] == s)
                return true;
        }
        return false;
    };
    
    auto var_is_global = [&](string & s) {
        for (size_t i = 0; i < var_defs[0].size(); i++)
        {
            if (var_defs[0][i] == s)
                return true;
        }
        return false;
    };
    
    unordered_map<string, TKind> trivial_ops;
    
    trivial_ops.insert({"call", Call});
    trivial_ops.insert({"call_eval", CallEval});
    trivial_ops.insert({"if_goto", IfGoto});
    trivial_ops.insert({"goto", Goto});
    trivial_ops.insert({"inc_goto_until", ForLoop});
    
    trivial_ops.insert({"<-", Assign});
    
    trivial_ops.insert({"[", ScopeOpen});
    trivial_ops.insert({"]~", ScopeClose});
    trivial_ops.insert({"]", ArrayBuild});
    
    trivial_ops.insert({"+", Add});
    trivial_ops.insert({"-", Sub});
    trivial_ops.insert({"*", Mul});
    trivial_ops.insert({"/", Div});
    trivial_ops.insert({"%", Mod});
    
    trivial_ops.insert({"+=", AddAssign});
    trivial_ops.insert({"-=", SubAssign});
    trivial_ops.insert({"*=", MulAssign});
    trivial_ops.insert({"/=", DivAssign});
    trivial_ops.insert({"%=", ModAssign});
    
    trivial_ops.insert({"&", And});
    trivial_ops.insert({"|", Or});
    trivial_ops.insert({"^", Xor});
    trivial_ops.insert({"<<", Shl});
    trivial_ops.insert({">>", Shr});
    
    trivial_ops.insert({"and", BoolAnd});
    trivial_ops.insert({"or", BoolOr});
    
    trivial_ops.insert({"==", CmpEQ});
    trivial_ops.insert({"!=", CmpNE});
    trivial_ops.insert({"<=", CmpLE});
    trivial_ops.insert({">=", CmpGE});
    trivial_ops.insert({"<", CmpLT});
    trivial_ops.insert({">", CmpGT});
    
    trivial_ops.insert({"::", Clone});
    //trivial_ops.insert({"::!", CloneDeep});
    
    trivial_ops.insert({"@", ArrayIndex});
    trivial_ops.insert({"@?", ArrayLen});
    trivial_ops.insert({"@?-", ArrayLenMinusOne});
    trivial_ops.insert({"@+", ArrayPushIn});
    trivial_ops.insert({"@-", ArrayPopOut});
    
    for (i = 0; i < program_texts.size() && program_texts[i] != ""; ++i)
    {
        string token = program_texts[i];

        if (token != "^^" && token.back() == '^' && token.size() >= 2)
        {
            var_defs.push_back({});
            program.push_back(make_token(FuncDec, get_token_func_num(token.substr(0, token.size() - 1))));
        }
        else if (token != "^^" && token.front() == '^' && token.size() >= 2)
            program.push_back(make_token(FuncLookup, get_token_func_num(token.substr(1))));
        else if (token == "^^")
        {
            var_defs.pop_back();
            program.push_back(make_token(FuncEnd, 0));
        }
        else if (token == "return")
            program.push_back(make_token(Return, 0));
        else if (token.front() == '$' && token.back() == '$' && token.size() >= 3)
        {
            auto s = token.substr(1, token.size() - 2);
            auto n = get_token_varname_num(s);
            var_defs.back().push_back(s);
            if (var_defs.size() > 1)
                program.push_back(make_token(LocalVarDecLookup, n));
            else
                program.push_back(make_token(GlobalVarDecLookup, n));
        }
        else if (token.back() == '$' && token.size() >= 2)
        {
            auto s = token.substr(0, token.size() - 1);
            auto n = get_token_varname_num(s);
            var_defs.back().push_back(s);
            if (var_defs.size() > 1)
                program.push_back(make_token(LocalVarDec, n));
            else
                program.push_back(make_token(GlobalVarDec, n));
        }
        else if (token.front() == '$' && token.size() >= 2)
        {
            auto s = token.substr(1);
            auto n = get_token_varname_num(s);
            if (var_is_local(s))
                program.push_back(make_token(LocalVarLookup, n));
            else if (var_is_global(s))
                program.push_back(make_token(GlobalVarLookup, n));
            else
                throw std::runtime_error("Undefined variable " + s + " on line " + std::to_string(lines[i]));
        }
        else if (token.front() == ':' && token.size() >= 2 && token != "::")
            program.push_back(make_token(LabelLookup, get_token_string_num(token.substr(1))));
        else if (token.back() == ':' && token.size() >= 2 && token != "::")
            program.push_back(make_token(LabelDec, get_token_string_num(token.substr(0, token.size() - 1))));
        else if (trivial_ops.count(token))
            program.push_back(make_token(trivial_ops[token], 0));
        else if (token.size() >= 2 && token[0] == '"' && token.back() == '"')
        {
            std::vector<DynamicType> str = {};
            for (size_t i = 1; i < token.size() - 1; i++)
                str.push_back((int64_t)token[i]);
            auto n = get_token_stringval_num(std::move(str));
            program.push_back(make_token(StringLiteral, n));
        }
        else if (token.size() >= 3 && token[0] == '"' && token.back() == '&')
        {
            auto str = make_shared<std::vector<DynamicType>>();
            for (size_t i = 1; i < token.size() - 2; i++)
                str->push_back((int64_t)token[i]);
            auto n = get_token_stringref_num(str);
            program.push_back(make_token(StringLitReference, n));
        }
        else if (token.size() >= 2 && token[0] == '!')
        {
            auto s = token.substr(1);
            // ->>> BUILTINS <<<-
            if (s == "print")
                program.push_back(make_token(BuiltinCall, 0));
            else if (s == "printstr")
                program.push_back(make_token(BuiltinCall, 1));
            else
                throw runtime_error("Unknown built-in function: " + token);
        }
        else
        {
            if (isint(token))
            {
                int64_t num = stoll(token);
                int64_t num2_smol = num / 10000;
                int64_t num2 = num2_smol * 10000;
                int64_t num3_smol = num >> 15;
                int64_t num3 = num3_smol << 15;
                
                int64_t d = (int64_t)1 << (sizeof(iword_t)*8-1);
                int64_t dhi = d-1;
                int64_t dlo = -d;
                
                if (num >= dlo && num <= dhi)
                    program.push_back(make_token(IntegerInline, (iword_t)num));
                else if (num2 == num && num2_smol >= dlo && num2_smol <= dhi)
                    program.push_back(make_token(IntegerInlineBigDec, (iword_t)num2_smol));
                else if (num3 == num && num3_smol >= dlo && num3_smol <= dhi)
                    program.push_back(make_token(IntegerInlineBigBin, (iword_t)num3_smol));
                else
                    program.push_back(make_token(Integer, get_token_int_num(num)));
            }
            else if (isfloat(token))
            {
                auto d = stod(token);
                
                uint64_t dec;
                memcpy(&dec, &d, sizeof(d));
                const auto x = 8 * (sizeof(uint64_t)-sizeof(iword_t));
                
                if ((dec >> x) << x == dec)
                    program.push_back(make_token(DoubleInline, (iword_t)(dec >> x)));
                else
                    program.push_back(make_token(Double, get_token_double_num(d)));
            }
            else if (isname(token))
            {
                auto n = get_token_varname_num(token);
                if (var_is_local(token) || var_is_global(token))
                    program.push_back(make_token(var_is_local(token) ? LocalVar : GlobalVar, n));
                else
                    throw std::runtime_error("Undefined variable " + token + " on line " + std::to_string(lines[i]));
            }
            else
                throw runtime_error("Invalid token: " + token);
        }
    }
    program.push_back(make_token(Exit, 0));
    
    auto prog_erase = [&](auto i) -> auto {
        program.erase(program.begin() + i);
        lines.erase(lines.begin() + i);
    };

    auto still_valid = [&]() { return i < program.size() && i + 1 < program.size() && program[i].kind != Exit && program[i + 1].kind != Exit; };

    // peephole optimizer!
    for (i = 0; (((ptrdiff_t)i) < 0 || i < program.size()) && program[i].kind != Exit && program[i + 1].kind != Exit; ++i)
    {
        if (still_valid() && program[i].kind == LocalVarLookup &&
            (program[i+1].kind == Assign || program[i+1].kind == Goto || program[i+1].kind == IfGoto))
        {
            program[i].kind = program[i+1].kind == Assign ? AssignVar : program[i+1].kind == Goto ? GotoLabel : IfGotoLabel;
            prog_erase(i-- + 1);
        }
        if (still_valid() && program[i].kind == LabelLookup && program[i+1].kind == ForLoop)
        {
            program[i].kind = ForLoopLabel;
            prog_erase(i-- + 1);
            i--;
        }
        if (still_valid() && i + 2 < program.size() && program[i].kind == LocalVarLookup && program[i+1].kind == IntegerInline && program[i+2].kind == ForLoopLabel)
        {
            program[i].kind = ForLoopFull;
            program[i].extra_1 = program[i].n;
            program[i].extra_2 = program[i+1].n;
            program[i].n = program[i+2].n;
            prog_erase(i + 2);
            prog_erase(i-- + 1);
        }
        if (still_valid() && program[i].kind >= CmpEQ && program[i].kind <= CmpGT && program[i+1].kind == IfGotoLabel)
        {
            program[i].kind = TKind(IfGotoLabelEQ + (program[i].kind - CmpEQ));
            program[i].n = program[i+1].n;
            prog_erase(i-- + 1);
        }
    }

    vector<Func> funcs;
    while (funcs.size() < token_funcs.size())
        funcs.push_back(Func{0,0,0});
    
    // rewrite in-function labels, register functions
    for (i = 0; i < program.size() && program[i].kind != Exit; i++)
    {
        auto & token = program[i];
        
        if (token.kind == FuncDec)
        {
            auto n = token.n;
            if (funcs[n].name != 0)
                throw std::runtime_error("Redefined function on or near line " + std::to_string(lines[i]));
            
            vector<iword_t> labels;
            
            while (labels.size() < token_strings.size())
                labels.push_back({(iword_t)-1});
            
            for (int i2 = i + 1; program[i2].kind != FuncEnd; i2 += 1)
            {
                if (program[i2].kind == LabelDec)
                {
                    labels[program[i2].n] = (iword_t)i2;
                    prog_erase(i2--);
                }
            }
            unsigned int start = i;
            unsigned int i2 = i + 1;
            for (; program[i2].kind != FuncEnd; i2 += 1)
            {
                if (program[i2].kind == LabelLookup || program[i2].kind == GotoLabel || program[i2].kind == ForLoopLabel || program[i2].kind == ForLoopFull ||
                    (program[i2].kind >= IfGotoLabel && program[i2].kind <= IfGotoLabelGT))
                {
                    program[i2].n = labels[program[i2].n];
                    if(program[i2].n == (iword_t)-1)
                        throw std::runtime_error("Unknown label usage on or near line " + std::to_string(lines[i2]));
                }
            }
            funcs[n] = Func{(iword_t)(start + 1), (iword_t)(i2 - start), n};
            
            // replace function def with a goto that jumps over the function def
            //token.kind = GotoLabel;
            //token.n = start + funcs[n].len + 1;
            // replace function num with the target position to jump to
            //token.n = start + funcs[n].len + 1;
        }
    }
    
    // rewrite root-level labels
    vector<iword_t> root_labels;
    while (root_labels.size() < token_strings.size())
        root_labels.push_back({(iword_t)-1});
    for (i = 0; i < program.size() && program[i].kind != Exit; i++)
    {
        if (program[i].kind == FuncDec) i += funcs[program[i].n].len;
        
        if (program[i].kind == LabelDec)
        {
            root_labels[program[i].n] = (iword_t)i;
            prog_erase(i--);
        }
    }
    // this needs to be a separate pass because labels can be seen upwards, not just downwards
    for (i = 0; i < program.size() && program[i].kind != Exit; ++i)
    {
        if (program[i].kind == FuncDec) i += funcs[program[i].n].len;
        
        if (program[i].kind == LabelLookup || program[i].kind == GotoLabel || program[i].kind == ForLoopLabel || program[i].kind == ForLoopFull ||
            (program[i].kind >= IfGotoLabel && program[i].kind <= IfGotoLabelGT))
        {
            program[i].n = root_labels[program[i].n];
            if (program[i].n == (iword_t)-1)
                throw std::runtime_error("Unknown label usage on or near line " + std::to_string(lines[i]));
        }
    }
    
    return {program, lines, funcs};
}

int interpret(const Program & programdata)
{
    auto & program = programdata.program;
    auto & lines = programdata.lines;
    auto & funcs = programdata.funcs;
    
    vector<DynamicType> vars_default;
    for (size_t i = 0; i < token_varnames.size(); i++)
        vars_default.push_back(0);
    
    vector<pair<iword_t, bool>> callstack;
    vector<iword_t> fstack;
    vector<DynamicType> globals = vars_default;
    vector<vector<DynamicType>> varstacks;
    vector<DynamicType> varstack;
    vector<vector<DynamicType>> evalstacks;
    vector<DynamicType> evalstack;
    
    fstack.push_back(0);
    
    auto valpush = [&](DynamicType x) { evalstack.push_back(x); };
    
    auto valpop = [&]() { return vec_pop_back(evalstack); };
    
    int i = 0;
    try
    {
        #ifdef INTERPRETER_USE_LOOP
        
        #define INTERPRETER_NEXT()
        #define INTERPRETER_DEF()\
            i = 0;\
            while (1) {\
                auto n = program[i].n;\
                switch (program[i].kind) {
        #define INTERPRETER_CASE(NAME) case NAME: i += 1; {
        #define INTERPRETER_ENDCASE() } break;
        #define INTERPRETER_ENDDEF() default: throw std::runtime_error("internal interpreter error: unknown opcode"); } }
        
        #else // of ifdef INTERPRETER_USE_LOOP
        
        static const void * const handlers[] = {
            &&HandlerGlobalVar,
            &&HandlerGlobalVarDec,
            &&HandlerGlobalVarLookup,
            &&HandlerGlobalVarDecLookup,
            
            &&HandlerLocalVar,
            &&HandlerLocalVarDec,
            &&HandlerLocalVarLookup,
            &&HandlerLocalVarDecLookup,
            
            &&HandlerFuncDec,
            &&HandlerFuncLookup,
            &&HandlerFuncEnd,
            
            &&HandlerLabelDec,
            &&HandlerLabelLookup,
            
            &&HandlerInteger,
            &&HandlerIntegerInline,
            &&HandlerIntegerInlineBigDec,
            &&HandlerIntegerInlineBigBin,
            &&HandlerDouble,
            &&HandlerDoubleInline,
            
            &&HandlerAdd,
            &&HandlerSub,
            &&HandlerMul,
            &&HandlerDiv,
            &&HandlerMod,
            
            &&HandlerAddAssign,
            &&HandlerSubAssign,
            &&HandlerMulAssign,
            &&HandlerDivAssign,
            &&HandlerModAssign,
            
            &&HandlerAnd,
            &&HandlerOr,
            &&HandlerXor,
            
            &&HandlerShl,
            &&HandlerShr,
            
            &&HandlerBoolAnd,
            &&HandlerBoolOr,
            
            &&HandlerAssign,
            &&HandlerAssignVar,
            &&HandlerReturn,
            &&HandlerCall,
            &&HandlerCallEval,
            
            &&HandlerIfGoto,
            &&HandlerIfGotoLabel,
            
            &&HandlerIfGotoLabelEQ,
            &&HandlerIfGotoLabelNE,
            &&HandlerIfGotoLabelLE,
            &&HandlerIfGotoLabelGE,
            &&HandlerIfGotoLabelLT,
            &&HandlerIfGotoLabelGT,
            
            &&HandlerGoto,
            &&HandlerGotoLabel,
            
            &&HandlerForLoop,
            &&HandlerForLoopLabel,
            &&HandlerForLoopFull,
            
            &&HandlerScopeOpen,
            &&HandlerScopeClose,
            
            &&HandlerCmpEQ,
            &&HandlerCmpNE,
            &&HandlerCmpLE,
            &&HandlerCmpGE,
            &&HandlerCmpLT,
            &&HandlerCmpGT,
            
            &&HandlerArrayBuild,
            &&HandlerArrayIndex,
            &&HandlerClone,
            //&&HandlerCloneDeep,
            
            &&HandlerArrayLen,
            &&HandlerArrayLenMinusOne,
            &&HandlerArrayPushIn,
            &&HandlerArrayPopOut,
            
            &&HandlerStringLiteral,
            &&HandlerStringLitReference,
            
            &&HandlerBuiltinCall,
            
            &&HandlerExit,
        };
        
        #define INTERPRETER_NEXT() goto *handlers[program[i].kind];
        #define INTERPRETER_DEF() goto AFTERDEFS;
        #define INTERPRETER_CASE(NAME)\
            { Handler##NAME : { \
            auto n = program[i++].n; (void)n;
            //printf("at %d in %s\n", i - 1, #NAME);
        #define INTERPRETER_ENDCASE() } INTERPRETER_NEXT() }
        #define INTERPRETER_ENDDEF() if (false) { AFTERDEFS: { INTERPRETER_NEXT() } }
        
        #endif // else of ifdef INTERPRETER_USE_LOOP
        
        
        #define INTERPRETER_MIDCASE(NAME) \
            INTERPRETER_ENDCASE()\
            INTERPRETER_CASE(NAME)
        
        INTERPRETER_DEF()
        
        INTERPRETER_CASE(FuncDec)
            // leaving this as an addition instead of a pre-cached assignment prevents the compiler from combining FuncDec with GotoLabel
            // and we WANT to do this prevention, for branch prediction reasons
            i += funcs[n].len;
        
        INTERPRETER_MIDCASE(FuncLookup)
            valpush(funcs[n]);
        
        INTERPRETER_MIDCASE(FuncEnd)
            varstack = vec_pop_back(varstacks);
            i = callstack.back().first;
            bool is_eval = callstack.back().second;
            callstack.pop_back();
            if (is_eval) valpush(0); // functions return 0 by default
        INTERPRETER_MIDCASE(Return)
            auto val = valpop();
            varstack = vec_pop_back(varstacks);
            i = callstack.back().first;
            bool is_eval = callstack.back().second;
            callstack.pop_back();
            if (is_eval) valpush(val);
        
        INTERPRETER_MIDCASE(LocalVarDec)
            varstack[n] = 0;
        INTERPRETER_MIDCASE(LocalVarLookup)
            valpush(Ref{&varstack[n]});
        INTERPRETER_MIDCASE(LocalVarDecLookup)
            varstack[n] = 0;
            valpush(Ref{&varstack[n]});
        INTERPRETER_MIDCASE(GlobalVarDec)
            globals[n] = 0;
        INTERPRETER_MIDCASE(GlobalVarLookup)
            valpush(Ref{&globals[n]});
        INTERPRETER_MIDCASE(GlobalVarDecLookup)
            globals[n] = 0;
            valpush(Ref{&globals[n]});
        
        INTERPRETER_MIDCASE(LabelLookup)
            valpush(Label{(int)n});
        INTERPRETER_MIDCASE(LabelDec)
            throw std::runtime_error("internal interpreter error: tried to execute opcode that's supposed to be deleted");
        
        INTERPRETER_MIDCASE(Call)
            Func f = valpop().as_func();
            callstack.push_back({i, 0});
            fstack.push_back(f.name);
            varstacks.push_back(std::move(varstack));
            varstack = vars_default;
            i = f.loc;
        INTERPRETER_MIDCASE(CallEval)
            Func f = valpop().as_func();
            callstack.push_back({i, 1});
            fstack.push_back(f.name);
            varstacks.push_back(std::move(varstack));
            varstack = vars_default;
            i = f.loc;
        
        INTERPRETER_MIDCASE(Assign)
            Ref ref = valpop().as_ref();
            auto val = valpop();
            *ref.ref = val;
        
        INTERPRETER_MIDCASE(AssignVar)
            auto val = valpop();
            varstack[n] = val;
        
        INTERPRETER_MIDCASE(ScopeOpen)
            evalstacks.push_back(std::move(evalstack));
            evalstack = {};
        INTERPRETER_MIDCASE(ScopeClose)
            evalstack = vec_pop_back(evalstacks);
        
        INTERPRETER_MIDCASE(IfGoto)
            Label dest = valpop().as_label();
            auto val = valpop();
            if (val) i = dest.loc;
        INTERPRETER_MIDCASE(IfGotoLabel)
            auto val = valpop();
            if (val) i = n;
        
        INTERPRETER_MIDCASE(ForLoop)
            Label dest = valpop().as_label();
            auto num = valpop();
            auto ref = valpop().as_ref();
            if (!num.is_int() || !ref.ref->is_int())
                throw std::runtime_error("Tried to use for loop with non-integer");
            *ref.ref = *ref.ref + 1;
            if (*ref.ref <= num)
                i = dest.loc;
        INTERPRETER_MIDCASE(ForLoopLabel)
            auto num = valpop();
            auto ref = valpop().as_ref();
            if (!num.is_int() || !ref.ref->is_int())
                throw std::runtime_error("Tried to use for loop with non-integer");
            *ref.ref = (*ref.ref) + 1;
            if (*ref.ref <= num)
                i = n;
        INTERPRETER_MIDCASE(ForLoopFull)
            auto & _v = varstack[program[i-1].extra_1];
            if (!_v.is_int())
                throw std::runtime_error("Tried to use for loop with non-integer");
            auto & v = _v.as_int();
            int64_t num = (iwordsigned_t)program[i-1].extra_2;
            v = v + 1;
            if (v <= num)
                i = n;
        
        #define INTERPRETER_MIDCASE_GOTOLABELCMP(X, OP) \
        INTERPRETER_MIDCASE(IfGotoLabel##X)\
            auto val2 = valpop();\
            auto val1 = valpop();\
            if (val1 OP val2) i = n;
        
        INTERPRETER_MIDCASE_GOTOLABELCMP(EQ, ==)
        INTERPRETER_MIDCASE_GOTOLABELCMP(NE, !=)
        INTERPRETER_MIDCASE_GOTOLABELCMP(LE, <=)
        INTERPRETER_MIDCASE_GOTOLABELCMP(GE, >=)
        INTERPRETER_MIDCASE_GOTOLABELCMP(LT, <)
        INTERPRETER_MIDCASE_GOTOLABELCMP(GT, >)
        
        INTERPRETER_MIDCASE(Goto)
            Label dest = valpop().as_label();
            i = dest.loc;
        INTERPRETER_MIDCASE(GotoLabel)
            i = n;
        
        #define INTERPRETER_MIDCASE_UNARY_SIMPLE(NAME, OP) \
        INTERPRETER_MIDCASE(NAME)\
            auto b = valpop();\
            auto a = valpop();\
            valpush(a OP b);
        
        #define INTERPRETER_MIDCASE_UNARY_ASSIGN(NAME, OP) \
        INTERPRETER_MIDCASE(NAME)\
            Ref ref = valpop().as_ref();\
            auto a = valpop();\
            auto & v = *ref.ref;\
            v = v OP a;
        
        INTERPRETER_MIDCASE_UNARY_SIMPLE(Add, +)
        INTERPRETER_MIDCASE_UNARY_SIMPLE(Sub, -)
        INTERPRETER_MIDCASE_UNARY_SIMPLE(Mul, *)
        INTERPRETER_MIDCASE_UNARY_SIMPLE(Div, /)
        INTERPRETER_MIDCASE_UNARY_SIMPLE(Mod, %)
        
        INTERPRETER_MIDCASE_UNARY_SIMPLE(And, &)
        INTERPRETER_MIDCASE_UNARY_SIMPLE(Or,  |)
        INTERPRETER_MIDCASE_UNARY_SIMPLE(Xor, ^)
        INTERPRETER_MIDCASE_UNARY_SIMPLE(BoolAnd, &&)
        INTERPRETER_MIDCASE_UNARY_SIMPLE(BoolOr, ||)
        
        INTERPRETER_MIDCASE_UNARY_SIMPLE(Shl, <<)
        INTERPRETER_MIDCASE_UNARY_SIMPLE(Shr, >>)
        
        INTERPRETER_MIDCASE_UNARY_ASSIGN(AddAssign, +)
        INTERPRETER_MIDCASE_UNARY_ASSIGN(SubAssign, -)
        INTERPRETER_MIDCASE_UNARY_ASSIGN(MulAssign, *)
        INTERPRETER_MIDCASE_UNARY_ASSIGN(DivAssign, /)
        INTERPRETER_MIDCASE_UNARY_ASSIGN(ModAssign, %)
        
        INTERPRETER_MIDCASE_UNARY_SIMPLE(CmpEQ, ==)
        INTERPRETER_MIDCASE_UNARY_SIMPLE(CmpNE, !=)
        INTERPRETER_MIDCASE_UNARY_SIMPLE(CmpLE, <=)
        INTERPRETER_MIDCASE_UNARY_SIMPLE(CmpGE, >=)
        INTERPRETER_MIDCASE_UNARY_SIMPLE(CmpLT, <)
        INTERPRETER_MIDCASE_UNARY_SIMPLE(CmpGT, >)
        
        INTERPRETER_MIDCASE(IntegerInline)
            valpush((int64_t)(iwordsigned_t)n);
        INTERPRETER_MIDCASE(IntegerInlineBigDec)
            valpush(((int64_t)(iwordsigned_t)n)*10000);
        INTERPRETER_MIDCASE(IntegerInlineBigBin)
            valpush(((int64_t)(iwordsigned_t)n)<<15);
        INTERPRETER_MIDCASE(Integer)
            valpush((int64_t)get_token_int(n));
        
        INTERPRETER_MIDCASE(DoubleInline)
            const auto x = 8 * (sizeof(uint64_t)-sizeof(iword_t));
            uint64_t dec = n;
            dec <<= x;
            double d;
            memcpy(&d, &dec, sizeof(dec));
            valpush(d);
        INTERPRETER_MIDCASE(Double)
            valpush(get_token_double(n));
        
        INTERPRETER_MIDCASE(LocalVar)
            valpush(varstack[n]);
        
        INTERPRETER_MIDCASE(GlobalVar)
            valpush(globals[n]);
        
        INTERPRETER_MIDCASE(ArrayBuild)
            auto back = std::move(evalstack);
            evalstack = vec_pop_back(evalstacks);
            valpush(Array{std::make_shared<vector<DynamicType>>(std::move(back))});
        
        INTERPRETER_MIDCASE(ArrayIndex)
            auto i = valpop().as_into_int();
            auto a = valpop();
            if (a.is_array())
            {
                auto v = (*a.as_array().items)[i];
                valpush(v);
            }
            else if (a.is_ref() && a.as_ref().ref->is_array())
            {
                auto * a2 = &a.as_ref().ref->as_array();
                auto * v = &(*a2->items)[i]; // FIXME holding on to pointers into the inside of arrays is extremely unsafe
                valpush({Ref{v}});
            }
            else
                throw std::runtime_error("Tried to index into a non-array value");
        
        INTERPRETER_MIDCASE(Clone)
            auto v = valpop();
            if (v.is_ref())
                v = *v.as_ref().ref;
            if (v.is_array())
                v.as_array().items = make_shared<vector<DynamicType>>(*v.as_array().items);
            valpush(v);
        
        INTERPRETER_MIDCASE(ArrayLen)
            auto a = valpop();
            if (a.is_array())
                valpush((int64_t)a.as_array().items->size());
            else if (a.is_ref() && a.as_ref().ref->is_array())
                valpush((int64_t)a.as_ref().ref->as_array().items->size());
            else
                throw std::runtime_error("Tried to get length of a non-array value");
        
        INTERPRETER_MIDCASE(ArrayLenMinusOne)
            auto a = valpop();
            if (a.is_array())
                valpush((int64_t)a.as_array().items->size() - 1);
            else if (a.is_ref() && a.as_ref().ref->is_array())
                valpush((int64_t)a.as_ref().ref->as_array().items->size() - 1);
            else
                throw std::runtime_error("Tried to get length of a non-array value");
        
        INTERPRETER_MIDCASE(ArrayPopOut)
            auto i = valpop().as_into_int();
            auto v = valpop();
            Array * a;
            if (v.is_array())
                a = &v.as_array();
            else if (v.is_ref() && v.as_ref().ref->is_array())
                a = &v.as_ref().ref->as_array();
            else
                throw std::runtime_error("Tried to index into a non-array value");
            auto ret = (*a->items)[i];
            a->items->erase(a->items->begin() + i);
            valpush(ret);
        
        INTERPRETER_MIDCASE(ArrayPushIn)
            auto inval = valpop();
            auto i = valpop().as_into_int();
            auto v = valpop();
            Array * a;
            if (v.is_array())
                a = &v.as_array();
            else if (v.is_ref() && v.as_ref().ref->is_array())
                a = &v.as_ref().ref->as_array();
            else
                throw std::runtime_error("Tried to index into a non-array value");
            auto ret = (*a->items)[i];
            a->items->insert(a->items->begin() + i, inval);
        
        INTERPRETER_MIDCASE(StringLiteral)
            valpush(Array{std::make_shared<vector<DynamicType>>(get_token_stringval(n))});
        
        INTERPRETER_MIDCASE(StringLitReference)
            valpush(Array{get_token_stringref(n)});
        
        INTERPRETER_MIDCASE(BuiltinCall)
            builtins[n](evalstack);
        
        INTERPRETER_MIDCASE(Exit)
            goto INTERPRETER_EXIT;
            INTERPRETER_ENDCASE()
        
        INTERPRETER_ENDDEF()

        INTERPRETER_EXIT: { }
        
        //printf("done! (token %d)\n", i);
        //printf("done!\n");
        //for (auto val : evalstack)
        //    printf("%.17g ", (val.is_int() ? val.as_int() : val.is_double() ? val.as_double() : (0.0/0.0)));
        //puts("");
    }
    catch (const exception& e)
    {
        printf("(token %d)\n", i);
        throw std::runtime_error("Error on line " + std::to_string(lines[i]) + ": " + e.what());
    }
    return 0;
}

int main(int argc, char ** argv)
{
    if (argc != 2)
        return puts("Usage: ./flinch <filename>"), 0;

    auto file = fopen(argv[1], "rb");
    if (!file)
        return printf("Failed to open file %s\n", argv[1]), 1;

    fseek(file, 0, SEEK_END);
    long int fsize = ftell(file);
    rewind(file);

    if (fsize < 0)
        return printf("Failed to read from file %s\n", argv[1]), 1;
    
    string text;
    text.resize(fsize);

    size_t bytes_read = fread(text.data(), 1, fsize, file);
    fclose(file);

    if (bytes_read != (size_t)fsize)
        return printf("Failed to read from file %s\n", argv[1]), 1;

    auto p = load_program(text);
    interpret(p);
}
