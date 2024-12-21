#ifndef FLINCH_INCLUDE
#define FLINCH_INCLUDE

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <variant>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <memory>
#include <algorithm>

#include <unordered_map>
#include <initializer_list>

#ifndef NOINLINE
#define NOINLINE __attribute__((noinline))
#endif

//#define THROWSTR(X) throw std::runtime_error(X)
//#define THROWSTR(X) throw (X)
template <typename T> [[noreturn]] void THROWSTR(T X) { throw std::runtime_error(X); }
//template <typename T> [[noreturn]] void THROWSTR(T X) { throw X; }

template <typename T>
[[noreturn]] void rethrow(int line, int i, T & e)
{
    THROWSTR("Error on line " + std::to_string(line) + ": " + e.what() + " (token " + std::to_string(i) + ")");
}

template <typename T> T vec_pop_back(std::vector<T> & v)
{
    T ret = std::move(v.back());
    v.pop_back();
    return ret;
}
template <typename T> T & vec_at_back(std::vector<T> & v)
{
    return v.at(v.size()-1);
}

using namespace std;

typedef uint32_t iword_t;
typedef  int32_t iwordsigned_t;
const int iword_bits_from_i64 = 8 * (sizeof(uint64_t)-sizeof(iword_t));

#define TOKEN_TABLE \
PFX(GlobalVar),PFX(GlobalVarDec),PFX(GlobalVarLookup),PFX(GlobalVarDecLookup),\
PFX(LocalVar),PFX(LocalVarDec),PFX(LocalVarLookup),PFX(LocalVarDecLookup),\
PFX(FuncDec),PFX(FuncLookup),PFX(FuncCall),PFX(FuncEnd),\
PFX(LabelDec),PFX(LabelLookup),\
PFX(Integer),\
PFX(IntegerInline),\
PFX(IntegerInlineBigDec),\
PFX(IntegerInlineBigBin),\
PFX(Double),\
PFX(DoubleInline),\
PFX(Add),PFX(Sub),PFX(Mul),PFX(Div),PFX(Mod),\
PFX(AddAssign),PFX(SubAssign),PFX(MulAssign),PFX(DivAssign),PFX(ModAssign),\
PFX(AddAsLocal),PFX(SubAsLocal),PFX(MulAsLocal),PFX(DivAsLocal),PFX(ModAsLocal),\
PFX(AddIntInline),PFX(SubIntInline),PFX(MulIntInline),PFX(DivIntInline),PFX(ModIntInline),\
PFX(AddDubInline),PFX(SubDubInline),PFX(MulDubInline),PFX(DivDubInline),PFX(ModDubInline),\
PFX(And),PFX(Or),PFX(Xor),\
PFX(Shl),PFX(Shr),\
PFX(BoolAnd),PFX(BoolOr),\
PFX(Assign),\
PFX(AsLocal),\
PFX(Return),PFX(Call),\
PFX(IfGoto),PFX(IfGotoLabel),\
PFX(IfGotoLabelEQ),PFX(IfGotoLabelNE),PFX(IfGotoLabelLE),\
PFX(IfGotoLabelGE),PFX(IfGotoLabelLT),PFX(IfGotoLabelGT),\
PFX(Goto),PFX(GotoLabel),\
PFX(ForLoop),PFX(ForLoopLabel),PFX(ForLoopLocal),\
PFX(ScopeOpen),PFX(ScopeClose),\
PFX(CmpEQ),PFX(CmpNE),PFX(CmpLE),PFX(CmpGE),PFX(CmpLT),PFX(CmpGT),\
PFX(ArrayBuild),\
PFX(ArrayIndex),\
PFX(Clone),\
PFX(CloneDeep),\
PFX(ArrayLen),PFX(ArrayLenMinusOne),PFX(ArrayPushIn),PFX(ArrayPopOut),PFX(ArrayConcat),\
PFX(StringLiteral),\
PFX(StringLitReference),\
PFX(BuiltinCall),\
PFX(Punt),\
PFX(PuntN),\
PFX(Exit)

// token kind
#define PFX(X) X
enum TKind : iword_t {
    TOKEN_TABLE,
    HandlerCount
};
#undef PFX

struct Token { TKind kind; iword_t n, extra_1, extra_2; };
struct Func { iword_t loc, len, name; };
Token make_token(TKind kind, iword_t n) { return {kind, n, 0, 0}; }

struct DynamicType;

typedef shared_ptr<vector<DynamicType>> ArrayData;

#ifndef MEMORY_UNSAFE_REFERENCES
struct Ref {
    struct RefInfo {
        // this backing vector will never be resized or reallocated, so it's OK to store a pointer directly into it
        ArrayData backing;
        DynamicType * refdata;
        size_t n;
    };
    RefInfo * info;
    DynamicType * ref() { return info->refdata; }
    ~Ref();
    
    Ref(RefInfo * r) noexcept : info(r) { }
    NOINLINE Ref(const Ref& r) noexcept { info = r.info; info->n += 1; }
    Ref(Ref&& r) noexcept { info = r.info; r.info = nullptr; }
    NOINLINE Ref & operator=(const Ref& r) noexcept { info = r.info; info->n += 1; return *this; }
    Ref & operator=(Ref&& r) noexcept { info = r.info; r.info = nullptr; return *this; }
};

NOINLINE Ref::~Ref() { if (!info) return; info->n -= 1; if (!info->n) delete info; }

#define MAKEREF return Ref{new Ref::RefInfo{backing, &(*backing).at(i), 1}};
#else // MEMORY_SAFE_REFERENCES
struct Ref {
    DynamicType * refdata;
    DynamicType * ref() { return refdata; }
    ~Ref();
};
NOINLINE Ref::~Ref() { } // ???? why does  this make the interpreter faster

#define MAKEREF return Ref{&(*backing).at(i)};
#endif // MEMORY_SAFE_REFERENCES

template <typename T>
ArrayData make_array_data(T x) { return make_shared<vector<DynamicType>>(x); }
ArrayData make_array_data() { return make_shared<vector<DynamicType>>(); }

struct Label { int loc; };
struct Array {
    struct ArrayInfo {
        ArrayData items;
        size_t n;
    };
    
    static ArrayInfo * make_arrayinfo(ArrayData && backing)
    {
        return new Array::ArrayInfo{backing, 1};
    }
    
    ArrayInfo * info;
    ArrayData & items();
    ~Array();
    // dirtify is called whenever doing anything that might resize the array
    // old references to inside of the array will remain pointing at valid memory, just stale and no longer actually point at the array
    // this is done in a way where reference copies of the entire array stay pointing at the same data as each other, hence the double shared_ptr
    // importantly, the ptr we're checking for uniqueness here is the *inner* one, not the outer one!
    void dirtify();
    
    Array(ArrayInfo * r) noexcept : info(r) { }
    NOINLINE Array(const Array & r) noexcept { info = r.info; info->n += 1; }
    Array(Array && r) noexcept { info = r.info; r.info = nullptr; }
    NOINLINE Array & operator=(const Array & r) noexcept { info = r.info; info->n += 1; return *this; }
    Array & operator=(Array && r) noexcept { info = r.info; r.info = nullptr; return *this; }
};
NOINLINE ArrayData & Array::items() { return info->items; }
NOINLINE Array make_array(ArrayData && backing) { return Array { Array::make_arrayinfo(std::move(backing)) };}
NOINLINE Array::~Array() { if (!info) return; info->n -= 1; if (!info->n) delete info; }

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
        else THROWSTR("Unsupported operation: non-numeric operands for operator " #OP " (or maybe) " #OP2);
    
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
        if (std::holds_alternative<TYPE>(value)) return std::get<TYPE>(value);\
        THROWSTR("Value is not of type " #TYPENAME);\
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
        THROWSTR("Value is not of a numeric type");
    }
    
    Array * as_array_ptr_thru_ref()
    {
        if (is_array()) return &as_array();
        else if (is_ref() && as_ref().ref()->is_array()) return &as_ref().ref()->as_array();
        else THROWSTR("Tried to use a non-array value as an array");
    }
        
    explicit operator bool() const
    {
        if (std::holds_alternative<int64_t>(value)) return !!std::get<int64_t>(value);
        else if (std::holds_alternative<double>(value)) return !!std::get<double>(value);
        return true;
    }
    
    DynamicType clone(bool deep)
    {
        while (is_ref()) *this = *as_ref().ref();
        if (!is_array()) return *this;
        auto t = make_array_data(*as_array().items());
        as_array().info = Array::make_arrayinfo(std::move(t));
        if (!deep) return *this;
        for (auto & item : *as_array().items()) item = item.clone(deep);
        return *this;
    }
};

inline Ref make_ref(ArrayData & backing, size_t i) { MAKEREF }

//NOINLINE void Array::dirtify() { if (info && info->n != 1) info->items = make_array_data(*info->items); }
void Array::dirtify() { if (info && info->n > 1) info->items = make_array_data(*info->items); }

struct Program {
    vector<Token> program;
    vector<int> lines;
    vector<Func> funcs;
    
    #define TOKEN_LOG(NAME, TYPE)\
    vector<TYPE> token_##NAME##s;\
    iword_t get_token_##NAME##_num(TYPE s)\
    {\
        for (iword_t i = 0; i < token_##NAME##s.size(); i++)\
        {\
            if (token_##NAME##s[i] == s) return i;\
        }\
        token_##NAME##s.push_back(s);\
        return token_##NAME##s.size() - 1;\
    }\
    TYPE get_token_##NAME(iword_t n) const { return token_##NAME##s[n]; }
    
    TOKEN_LOG(string, string)
    TOKEN_LOG(func, string)
    TOKEN_LOG(varname, string)
    TOKEN_LOG(int, int64_t)
    TOKEN_LOG(double, double)
    TOKEN_LOG(stringval, vector<DynamicType>)
    TOKEN_LOG(stringref, ArrayData)
};

// built-in function definitions. must be specifically here. do not move.
#include "builtins.hpp"

Program load_program(string text)
{
    size_t line = 0;
    size_t i = 0;
    
    Program programdata;
    vector<string> program_texts;
    
    auto & program = programdata.program;
    auto & lines = programdata.lines;
    auto & funcs = programdata.funcs;
    
    while (i < text.size())
    {
        while (i < text.size() && isspace(text[i]))
        {
            if (text[i++] == '\n') line++;
        }

        if (i < text.size() && text[i] == '#')
        {
            while (i < text.size() && text[i] != '\n') i++;
        }
        else
        {
            size_t start_i = i;
            if (text[i] == '\'')
            {
                i += 3 + (text[i+1] == '\\');
                if (i >= text.size() || i < start_i || text[i-1] != '\'')
                    THROWSTR("Char literal must be a single char or a \\ followed by a single char on line " + std::to_string(line));
            }
            else if (text[i] == '"')
            {
                i++;
                string s = "\"";
                while (i < text.size())
                {
                    if (text[i] == '\\' && i + 1 < text.size())
                    {
                        if (text[i+1] == 'x' && i + 3 < text.size())
                            s += std::strtol(text.substr(i+2, 2).data(), nullptr, 16);
                        else if (text[i+1] == 'n') s += '\n';
                        else if (text[i+1] == 'r') s += '\r';
                        else if (text[i+1] == 't') s += '\t';
                        else if (text[i+1] == '\\') s += '\\';
                        else if (text[i+1] == '\0') s += '\0';
                        else s += text[i+1];
                        i += 1;
                    }
                    else
                        s += text[i];
                    if (s.back() == '"') break;
                    i += 1;
                }
                
                // reference-type strings
                if (text[++i] == '*') s += text[i++];
                
                if (!isspace(text[i]) && text[i] != 0)
                    THROWSTR("String literals must not have trailing text after the closing \" character");
            }
            else
            {
                while (i < text.size() && !isspace(text[i])) i++;
            }
            program_texts.push_back(text.substr(start_i, i - start_i));
            lines.push_back(line + 1);
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
            if (!std::isdigit(str[i])) return false;
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
        catch (...) { }
        return false;
    };
    
    auto isname = [&](const string& str) {
        if (str.empty() || (!isalpha(str[0]) && str[0] != '_'))
            return false;
        for (size_t i = 1; i < str.size(); i++)
        {
            if (!isalnum(str[i]) && str[i] != '_') return false;
        }
        return true;
    };
    
    programdata.get_token_func_num("");
    
    vector<vector<string>> var_defs = {{}};
    
    auto var_is_local = [&](string & s) {
        if (var_defs.size() == 1) return false;
        return std::find(var_defs.back().begin(), var_defs.back().end(), s) != var_defs.back().end();
    };
    
    auto var_is_global = [&](string & s) {
        return std::find(var_defs[0].begin(), var_defs[0].end(), s) != var_defs[0].end();
    };
    
    auto shunting_yard = [&](size_t i) {
        size_t start_i = i;
        program_texts.erase(program_texts.begin() + i); // erase leading paren
        lines.erase(lines.begin() + i);
        
        unordered_map<string, int> prec;
        
        #define ADD_PREC(N, X) for (auto & s : X) prec.insert({s, N});
        ADD_PREC(6, (initializer_list<string>{ "@", "@-", "@+", "@@" }));
        ADD_PREC(5, (initializer_list<string>{ "*", "/", "%", "<<", ">>", "&" }));
        ADD_PREC(4, (initializer_list<string>{ "+", "-", "|", "^" }));
        ADD_PREC(3, (initializer_list<string>{ "==", "<=", ">=", "!=", ">", "<" }));
        ADD_PREC(2, (initializer_list<string>{ "and", "or" }));
        ADD_PREC(1, (initializer_list<string>{ "->", "+=", "-=", "*=", "/=", "%=" }));
        ADD_PREC(0, (initializer_list<string>{ ";" }));
        
        vector<int> nums, ops;
        
        while (i < program_texts.size() && program_texts[i] != ")")
        {
            if (program_texts[i] == string("(" )|| program_texts[i] == string("(("))
            {
                for (int x = 0; i < program_texts.size() && (x || program_texts[i] == string("(") || program_texts[i] == string("((")); i++)
                {
                    x += (program_texts[i] == string("(" )|| program_texts[i] == string("((")) - (program_texts[i] == string(")") || program_texts[i] == string("))"));
                    nums.push_back(i);
                }
            }
            else if (!prec.count(program_texts[i]))
                nums.push_back(i++);
            else
            {
                while (ops.size() && prec[program_texts[i]] < prec[program_texts[ops.back()]])
                    nums.push_back(vec_pop_back(ops));
                ops.push_back(i++);
            }
        }
        while (ops.size()) nums.push_back(vec_pop_back(ops));
        
        if (i >= program_texts.size())
            THROWSTR("Paren expression must end in a closing paren, i.e. ')', starting on or near line " + to_string(lines[start_i]));
        
        // parallel move; we go through this effort to keep lines and texts in sync
        vector<string> texts_s;
        for (size_t j = 0; j < nums.size(); j++)
            texts_s.push_back(std::move(program_texts[nums[j]]));
        for (size_t j = 0; j < nums.size(); j++)
            program_texts[j + start_i] = std::move(texts_s[j]);
        
        vector<int> lines_s;
        for (size_t j = 0; j < nums.size(); j++)
            lines_s.push_back(lines[nums[j]]);
        for (size_t j = 0; j < nums.size(); j++)
            lines[j + start_i] = lines_s[j];
        
        program_texts.erase(program_texts.begin() + i);
        lines.erase(lines.begin() + i);
    };
    
    unordered_map<string, TKind> trivial_ops;
    
    trivial_ops.insert({"call", Call});
    trivial_ops.insert({"if_goto", IfGoto});
    trivial_ops.insert({"goto", Goto});
    trivial_ops.insert({"inc_goto_until", ForLoop});
    trivial_ops.insert({"punt", Punt});
    trivial_ops.insert({"punt_n", PuntN});
    
    trivial_ops.insert({"->", Assign});
    
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
    trivial_ops.insert({"::!", CloneDeep});
    
    trivial_ops.insert({"@", ArrayIndex});
    trivial_ops.insert({"@?", ArrayLen});
    trivial_ops.insert({"@?-", ArrayLenMinusOne});
    trivial_ops.insert({"@+", ArrayPushIn});
    trivial_ops.insert({"@-", ArrayPopOut});
    trivial_ops.insert({"@@", ArrayConcat});
    
    for (i = 0; i < program_texts.size() && program_texts[i] != ""; i++)
    {
        string & token = program_texts[i];

        if (token == "(")
            shunting_yard(i--);
        else if (token == "((" || token == "))" || token == ";")
            lines.erase(lines.begin() + i);
        else if (token != "^^" && token.size() >= 2 && token.back() == '^')
        {
            var_defs.push_back({});
            program.push_back(make_token(FuncDec, programdata.get_token_func_num(token.substr(0, token.size() - 1))));
        }
        else if (token.size() >= 2 && token.front() == '.')
            program.push_back(make_token(FuncCall, programdata.get_token_func_num(token.substr(1))));
        else if (token != "^^" && token.size() >= 2 && token.front() == '^')
            program.push_back(make_token(FuncLookup, programdata.get_token_func_num(token.substr(1))));
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
            auto n = programdata.get_token_varname_num(s);
            var_defs.back().push_back(s);
            if (var_defs.size() > 1)
                program.push_back(make_token(LocalVarDecLookup, n));
            else
                program.push_back(make_token(GlobalVarDecLookup, n));
        }
        else if (token.back() == '$' && token.size() >= 2)
        {
            auto s = token.substr(0, token.size() - 1);
            auto n = programdata.get_token_varname_num(s);
            var_defs.back().push_back(s);
            if (var_defs.size() > 1)
                program.push_back(make_token(LocalVarDec, n));
            else
                program.push_back(make_token(GlobalVarDec, n));
        }
        else if (token.front() == '$' && token.size() >= 2)
        {
            auto s = token.substr(1);
            auto n = programdata.get_token_varname_num(s);
            if (var_is_local(s))
                program.push_back(make_token(LocalVarLookup, n));
            else if (var_is_global(s))
                program.push_back(make_token(GlobalVarLookup, n));
            else
                THROWSTR("Undefined variable " + s + " on line " + std::to_string(lines[i]));
        }
        else if (token.front() == ':' && token.size() >= 2 && token != "::" && token != "::!")
            program.push_back(make_token(LabelLookup, programdata.get_token_string_num(token.substr(1))));
        else if (token.back() == ':' && token.size() >= 2 && token != "::" && token != "::!")
            program.push_back(make_token(LabelDec, programdata.get_token_string_num(token.substr(0, token.size() - 1))));
        else if (trivial_ops.count(token))
            program.push_back(make_token(trivial_ops[token], 0));
        else if (token.size() >= 3 && token[0] == '"' && token.back() == '*') // '"' (fix tokei line counts)
        {
            vector<DynamicType> str = {};
            for (size_t i = 1; i < token.size() - 2; i++)
                str.push_back((int64_t)token[i]);
            auto n = programdata.get_token_stringval_num(std::move(str));
            program.push_back(make_token(StringLiteral, n));
        }
        else if (token.size() >= 2 && token[0] == '"' && token.back() == '"')
        {
            auto str = make_array_data();
            for (size_t i = 1; i < token.size() - 1; i++)
                str->push_back((int64_t)token[i]);
            auto n = programdata.get_token_stringref_num(str);
            program.push_back(make_token(StringLitReference, n));
        }
        else if (token.size() >= 2 && token[0] == '!')
        {
            auto s = token.substr(1);
            program.push_back(make_token(BuiltinCall, builtins_lookup(s)));
        }
        else if ((token.size() == 3 || token.size() == 4) && token[0] == '\'' && token.back() == '\'')
        {
            if (token.size() == 3) program.push_back(make_token(IntegerInline, token[1]));
            else if (token.size() != 4 || token[1] != '\\') THROWSTR("Char literal must be a single char or a \\ followed by a single char on line " + std::to_string(lines[i]));
            else if (token[2] == 'n') program.push_back(make_token(IntegerInline, '\n'));
            else if (token[2] == 'r') program.push_back(make_token(IntegerInline, '\r'));
            else if (token[2] == 't') program.push_back(make_token(IntegerInline, '\t'));
            else if (token[2] == '\\') program.push_back(make_token(IntegerInline, '\\'));
            else program.push_back(make_token(IntegerInline, token[2]));
        }
        else
        {
            if (isint(token))
            {
                int sbits = (sizeof(iword_t)*8-1);
                int64_t num = stoll(token);
                int64_t num2_smol = num  / (sbits == 15 ? 10000 : sbits == 31 ? 1000000000 : sbits == 63 ? 1 : 50);
                int64_t num2 = num2_smol * (sbits == 15 ? 10000 : sbits == 31 ? 1000000000 : sbits == 63 ? 1 : 50);
                int64_t num3_smol = num >> sbits;
                int64_t num3 = num3_smol << sbits;
                
                int64_t d = (int64_t)1 << sbits;
                int64_t dhi = d-1;
                int64_t dlo = -d;
                
                if (num >= dlo && num <= dhi)
                    program.push_back(make_token(IntegerInline, (iword_t)num));
                else if (num2 == num && num2_smol >= dlo && num2_smol <= dhi)
                    program.push_back(make_token(IntegerInlineBigDec, (iword_t)num2_smol));
                else if (num3 == num && num3_smol >= dlo && num3_smol <= dhi)
                    program.push_back(make_token(IntegerInlineBigBin, (iword_t)num3_smol));
                else
                    program.push_back(make_token(Integer, programdata.get_token_int_num(num)));
            }
            else if (isfloat(token))
            {
                auto d = stod(token);
                
                uint64_t dec;
                memcpy(&dec, &d, sizeof(d));
                
                if ((dec >> iword_bits_from_i64) << iword_bits_from_i64 == dec)
                    program.push_back(make_token(DoubleInline, (iword_t)(dec >> iword_bits_from_i64)));
                else
                    program.push_back(make_token(Double, programdata.get_token_double_num(d)));
            }
            else if (isname(token))
            {
                auto n = programdata.get_token_varname_num(token);
                if (var_is_local(token) || var_is_global(token))
                    program.push_back(make_token(var_is_local(token) ? LocalVar : GlobalVar, n));
                else
                    THROWSTR("Undefined variable " + token + " on line " + std::to_string(lines[i]));
            }
            else
                THROWSTR("Invalid token: " + token);
        }
    }
    program.push_back(make_token(Exit, 0));
    
    //for (auto & s : program_texts)
    //    printf("%s\n", s.data());
    
    auto prog_erase = [&](auto i) -> auto {
        program.erase(program.begin() + i);
        lines.erase(lines.begin() + i);
    };

    auto still_valid = [&]() { return i < program.size() && i + 1 < program.size() && program[i].kind != Exit && program[i + 1].kind != Exit; };

    // peephole optimizer!
    for (i = 0; ((ptrdiff_t)i) < 0 || (i < program.size() && program[i].kind != Exit && program[i + 1].kind != Exit); ++i)
    {
        if (still_valid() && program[i].kind == IntegerInline && (program[i+1].kind == Add || program[i+1].kind == Sub ||
            program[i+1].kind == Mul || program[i+1].kind == Div || program[i+1].kind == Mod))
        {
            program[i].kind = (TKind)(program[i+1].kind + (AddIntInline - Add));
            prog_erase(i-- + 1);
        }
        if (still_valid() && program[i].kind == DoubleInline && (program[i+1].kind == Add || program[i+1].kind == Sub ||
            program[i+1].kind == Mul || program[i+1].kind == Div || program[i+1].kind == Mod))
        {
            program[i].kind = (TKind)(program[i+1].kind + (AddDubInline - Add));
            prog_erase(i-- + 1);
        }
        if (still_valid() && program[i].kind == LocalVarLookup && (program[i+1].kind == AddAssign || program[i+1].kind == SubAssign ||
             program[i+1].kind == MulAssign || program[i+1].kind == DivAssign || program[i+1].kind == ModAssign))
        {
            program[i].kind = (TKind)(program[i+1].kind + (AddAsLocal - AddAssign));
            prog_erase(i-- + 1);
        }
        if (still_valid() && program[i].kind == LocalVarLookup &&
            (program[i+1].kind == Assign || program[i+1].kind == Goto || program[i+1].kind == IfGoto))
        {
            program[i].kind = program[i+1].kind == Assign ? AsLocal : program[i+1].kind == Goto ? GotoLabel : IfGotoLabel;
            prog_erase(i-- + 1);
        }
        if (still_valid() && program[i].kind == LabelLookup && program[i+1].kind == ForLoop)
        {
            program[i].kind = ForLoopLabel;
            prog_erase(i-- + 1);
            // need to be able to see the output of this optimization one token to the left, for the next optimization
            i--;
        }
        if (still_valid() && i + 2 < program.size() && program[i].kind == LocalVarLookup && program[i+1].kind == IntegerInline && program[i+2].kind == ForLoopLabel)
        {
            program[i].kind = ForLoopLocal;
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

    //for (auto & s : program)
    //    printf("%d\n", s.kind);
    
    funcs = vector<Func>(programdata.token_funcs.size());
    std::fill(funcs.begin(), funcs.end(), Func{0,0,0});
    
    vector<iword_t> root_labels(programdata.token_strings.size());
    std::fill(root_labels.begin(), root_labels.end(), (iword_t)-1);
    
    // rewrite in-function labels, register functions
    // also rewrite root-level labels
    for (i = 0; i < program.size() && program[i].kind != Exit; i++)
    {
        if (program[i].kind == FuncDec)
        {
            if (funcs[program[i].n].name != 0)
                THROWSTR("Redefined function on or near line " + std::to_string(lines[i]));
            
            vector<iword_t> labels(programdata.token_strings.size());
            std::fill(labels.begin(), labels.end(), (iword_t)-1);
            
            for (size_t i2 = i + 1; program[i2].kind != FuncEnd; i2 += 1)
            {
                if (program[i2].kind == LabelDec)
                {
                    labels[program[i2].n] = (iword_t)i2;
                    prog_erase(i2--);
                }
            }
            
            // this needs to be a separate pass because labels can be seen upwards, not just downwards
            size_t i2 = i + 1;
            for (; program[i2].kind != FuncEnd; i2 += 1)
            {
                if (program[i2].kind == LabelLookup || program[i2].kind == GotoLabel || program[i2].kind == ForLoopLabel ||
                    program[i2].kind == ForLoopLocal || (program[i2].kind >= IfGotoLabel && program[i2].kind <= IfGotoLabelGT))
                {
                    program[i2].n = labels[program[i2].n];
                    if(program[i2].n == (iword_t)-1)
                        THROWSTR("Unknown label usage on or near line " + std::to_string(lines[i2]));
                }
            }
            funcs[program[i].n] = Func{(iword_t)(i + 1), (iword_t)(i2 - i), program[i].n};
            i = i2;
        }
        else if (program[i].kind == LabelDec)
        {
            root_labels[program[i].n] = (iword_t)i;
            prog_erase(i--);
        }
    }
    
    // rewrite global labels
    // this needs to be a separate pass because labels can be seen upwards, not just downwards
    for (i = 0; i < program.size() && program[i].kind != Exit; i++)
    {
        if (program[i].kind == FuncDec) i += funcs[program[i].n].len;
        
        if (program[i].kind == LabelLookup || program[i].kind == GotoLabel || program[i].kind == ForLoopLabel ||
            program[i].kind == ForLoopLocal || (program[i].kind >= IfGotoLabel && program[i].kind <= IfGotoLabelGT))
        {
            program[i].n = root_labels[program[i].n];
            if (program[i].n == (iword_t)-1)
                THROWSTR("Unknown label usage on or near line " + std::to_string(lines[i]));
        }
    }
    return programdata;
}

struct ProgramState {
    const Program & programdata;
    const vector<Func> & funcs;
    vector<DynamicType> vars_default;
    vector<iword_t> callstack, fstack;
    
    ArrayData globals;
    DynamicType * globals_raw;
    
    vector<ArrayData> varstacks;
    ArrayData varstack;
    DynamicType * varstack_raw;
    
    vector<vector<DynamicType>> evalstacks;
    vector<DynamicType> evalstack;
};

#if !defined(INTERPRETER_USE_LOOP) && !defined(INTERPRETER_USE_CGOTO)
typedef void(*[[clang::preserve_none]] HandlerT)(ProgramState & s, int i, const Token * program);
struct HandlerInfo { const HandlerT s[HandlerCount]; };
extern const HandlerInfo handler;
#endif

int interpreter_core(const Program & programdata, int i)
{
    auto program = programdata.program.data();
    
    vector<DynamicType> vars_default;
    for (size_t i = 0; i < programdata.token_varnames.size(); i++)
        vars_default.push_back(0);
    
    auto s = ProgramState{
        programdata, programdata.funcs, vars_default, {}, {},
        make_array_data(vars_default), 0, {}, make_array_data(vars_default), 0, {}, {}
    };
    
    s.globals_raw = s.globals->data();
    s.varstack_raw = s.varstack->data();
    s.fstack.push_back(0);
    
    #define valreq(X) if (s.evalstack.size() < X) THROWSTR("internal interpreter error: not enough values on stack");
    #define valpush(X) s.evalstack.push_back(X)
    #define valpop() vec_pop_back(s.evalstack)
    #define valback() vec_at_back(s.evalstack)
    
    #ifdef INTERPRETER_USE_LOOP
    
    #define INTERPRETER_NEXT()
    #define INTERPRETER_DEF() try { while (1) {\
            auto n = program[i].n;\
            switch (program[i].kind) {
    #define INTERPRETER_CASE(NAME) case NAME: i += 1; {
    #define INTERPRETER_ENDCASE() } break;
    #define INTERPRETER_ENDDEF() default: THROWSTR("internal interpreter error: unknown opcode"); } } }\
        catch (const exception& e) { rethrow(s.programdata.lines[i], i, e); }
    #define INTERPRETER_DOEXIT() return 0;
    
    #elif defined INTERPRETER_USE_CGOTO
    
    #define PFX(X) &&Handler##X
    const void * const handlers[] = { TOKEN_TABLE };
    #undef PFX
    
    #define INTERPRETER_NEXT() { goto *handlers[program[i].kind]; }
    #define INTERPRETER_DEF() try { { goto *handlers[program[i].kind]; }
    
    #define INTERPRETER_CASE(NAME)\
        { Handler##NAME: \
        auto n = program[i++].n; (void)n; {
        //printf("at %d in %s\n", i - 1, #NAME);
    #define INTERPRETER_ENDCASE() } INTERPRETER_NEXT() }
    #define INTERPRETER_ENDDEF() INTERPRETER_EXIT: { } return 0; }\
        catch (const exception& e) { rethrow(s.programdata.lines[i], i, e); }
    #define INTERPRETER_DOEXIT() goto INTERPRETER_EXIT;
    
    #else // of ifdef INTERPRETER_USE_LOOP
    
    #define INTERPRETER_NEXT() { [[clang::musttail]] return handler.s[program[i].kind](s, i, program); }
    #define INTERPRETER_DEF() { handler.s[program[i].kind](s, i, program); return 0; } }
    
    #define INTERPRETER_CASE(NAME)\
        [[clang::preserve_none]] void Handler##NAME(ProgramState & s, int i, const Token * program) { \
        auto n = program[i++].n; (void)n; try {
        //printf("at %d in %s\n", i - 1, #NAME);
    #define INTERPRETER_ENDCASE() } catch (const exception& e) { rethrow(s.programdata.lines[i], i, e); }\
        INTERPRETER_NEXT() }
    #define INTERPRETER_ENDDEF() void _aowsgawgioaefwe(void){
    #define INTERPRETER_DOEXIT() return;
    
    #endif // else of ifdef INTERPRETER_USE_LOOP
    
    #define INTERPRETER_MIDCASE(NAME) INTERPRETER_ENDCASE() INTERPRETER_CASE(NAME)
    
    INTERPRETER_DEF()
    
    INTERPRETER_CASE(FuncDec)
        // leaving this as an addition instead of a pre-cached assignment prevents the compiler from combining FuncDec with GotoLabel
        // and we WANT to do this prevention, for branch prediction reasons
        i += s.funcs[n].len;
    
    INTERPRETER_MIDCASE(FuncLookup)
        valpush(s.funcs[n]);
    
    INTERPRETER_MIDCASE(FuncEnd)
        s.varstack = vec_pop_back(s.varstacks);
        s.varstack_raw = s.varstack->data();
        i = vec_pop_back(s.callstack);
    INTERPRETER_MIDCASE(Return)
        s.varstack = vec_pop_back(s.varstacks);
        s.varstack_raw = s.varstack->data();
        i = vec_pop_back(s.callstack);
    
    INTERPRETER_MIDCASE(LocalVarDec)
        s.varstack_raw[n] = 0;
    INTERPRETER_MIDCASE(LocalVarLookup)
        valpush(make_ref(s.varstack, n));
    INTERPRETER_MIDCASE(LocalVarDecLookup)
        s.varstack_raw[n] = 0;
        valpush(make_ref(s.varstack, n));
    INTERPRETER_MIDCASE(GlobalVarDec)
        s.globals_raw[n] = 0;
    INTERPRETER_MIDCASE(GlobalVarLookup)
        valpush(make_ref(s.globals, n));
    INTERPRETER_MIDCASE(GlobalVarDecLookup)
        s.globals_raw[n] = 0;
        valpush(make_ref(s.globals, n));
    
    INTERPRETER_MIDCASE(LabelLookup)
        valpush(Label{(int)n});
    INTERPRETER_MIDCASE(LabelDec)
        THROWSTR("internal interpreter error: tried to execute opcode that's supposed to be deleted");
    
    INTERPRETER_MIDCASE(Call)
        Func f = valpop().as_func();
        s.callstack.push_back(i);
        s.fstack.push_back(f.name);
        s.varstacks.push_back(s.varstack);
        s.varstack = make_array_data(s.vars_default);
        s.varstack_raw = s.varstack->data();
        i = f.loc;
    
    INTERPRETER_MIDCASE(FuncCall)
        Func f = s.funcs[n];
        s.callstack.push_back(i);
        s.fstack.push_back(f.name);
        s.varstacks.push_back(s.varstack);
        s.varstack = make_array_data(s.vars_default);
        s.varstack_raw = s.varstack->data();
        i = f.loc;
    
    INTERPRETER_MIDCASE(Assign) valreq(2);
        Ref ref = std::move(valpop().as_ref());
        auto val = valpop();
        *ref.ref() = val;
    
    INTERPRETER_MIDCASE(AsLocal)
        auto val = valpop();
        s.varstack_raw[n] = val;
    
    INTERPRETER_MIDCASE(ScopeOpen)
        s.evalstacks.push_back(std::move(s.evalstack));
        s.evalstack = {};
    INTERPRETER_MIDCASE(ScopeClose)
        s.evalstack = vec_pop_back(s.evalstacks);
    
    INTERPRETER_MIDCASE(IfGoto) valreq(2);
        Label dest = valpop().as_label();
        auto val = valpop();
        if (val) i = dest.loc;
    INTERPRETER_MIDCASE(IfGotoLabel)
        auto val = valpop();
        if (val) i = n;
    
    INTERPRETER_MIDCASE(ForLoop) valreq(3);
        Label dest = valpop().as_label();
        auto num = valpop();
        auto ref = std::move(valpop().as_ref());
        if (!num.is_int() || !ref.ref()->is_int())
            THROWSTR("Tried to use for loop with non-integer");
        *ref.ref() = *ref.ref() + 1;
        if (*ref.ref() < num) i = dest.loc;
    INTERPRETER_MIDCASE(ForLoopLabel) valreq(2);
        auto num = valpop();
        auto ref = std::move(valpop().as_ref());
        if (!num.is_int() || !ref.ref()->is_int())
            THROWSTR("Tried to use for loop with non-integer");
        *ref.ref() = *ref.ref() + 1;
        if (*ref.ref() < num) i = n;
    INTERPRETER_MIDCASE(ForLoopLocal)
        // FIXME: add a global version
        auto & _v = s.varstack_raw[program[i-1].extra_1];
        if (!_v.is_int())
            THROWSTR("Tried to use for loop with non-integer");
        auto & v = _v.as_int();
        int64_t num = (iwordsigned_t)program[i-1].extra_2;
        if (++v < num) i = n;
    
    // INTERPRETER_MIDCASE_GOTOLABELCMP
    #define IMGLC(X, OP) \
    INTERPRETER_MIDCASE(IfGotoLabel##X) valreq(2);\
        auto val2 = valpop();\
        auto val1 = valpop();\
        if (val1 OP val2) i = n;
    
    // INTERPRETER_MIDCASE_UNARY_SIMPLE
    #define IMCUS(NAME, OP) \
    INTERPRETER_MIDCASE(NAME) valreq(2);\
        auto b = valpop();\
        auto x = valpop();\
        valpush(x OP b);
    
    // INTERPRETER_MIDCASE_UNARY_INTINLINE
    #define IMCUII(NAME, OP) \
    INTERPRETER_MIDCASE(NAME)\
        auto b = (int64_t)(iwordsigned_t)n;\
        auto x = valpop();\
        valpush(x OP b);
    
    // INTERPRETER_MIDCASE_UNARY_DUBINLINE
    #define IMCUDI(NAME, OP) \
    INTERPRETER_MIDCASE(NAME)\
        uint64_t dec = ((uint64_t)n) << iword_bits_from_i64;\
        double d;\
        memcpy(&d, &dec, sizeof(dec));\
        auto x = valpop();\
        valpush(x OP d);
    
    // INTERPRETER_MIDCASE_UNARY_ASSIGN
    #define IMCUA(NAME, OP) \
    INTERPRETER_MIDCASE(NAME) valreq(2);\
        Ref ref = std::move(valpop().as_ref());\
        auto a = valpop();\
        *ref.ref() = *ref.ref() OP a;
    
    // INTERPRETER_MIDCASE_UNARY_ASSIGNLOC
    #define IMCUAL(NAME, OP) \
    INTERPRETER_MIDCASE(NAME)\
        auto a = valpop();\
        s.varstack_raw[n] = s.varstack_raw[n] OP a;
    
    IMGLC(EQ, ==) IMGLC(NE, !=) IMGLC(LE, <=) IMGLC(GE, >=) IMGLC(LT, <) IMGLC(GT, >)
    IMCUS(Add, +) IMCUS(Sub, -) IMCUS(Mul, *) IMCUS(Div, /) IMCUS(Mod, %)
    IMCUS(And, &) IMCUS(Or,  |) IMCUS(Xor, ^) IMCUS(BoolAnd, &&) IMCUS(BoolOr, ||)
    IMCUS(Shl, <<) IMCUS(Shr, >>)
    IMCUS(CmpEQ, ==) IMCUS(CmpNE, !=) IMCUS(CmpLE, <=) IMCUS(CmpGE, >=) IMCUS(CmpLT, <) IMCUS(CmpGT, >)
    IMCUII(AddIntInline, +) IMCUII(SubIntInline, -) IMCUII(MulIntInline, *) IMCUII(DivIntInline, /) IMCUII(ModIntInline, %)
    IMCUDI(AddDubInline, +) IMCUDI(SubDubInline, -) IMCUDI(MulDubInline, *) IMCUDI(DivDubInline, /) IMCUDI(ModDubInline, %)
    IMCUA(AddAssign, +) IMCUA(SubAssign, -) IMCUA(MulAssign, *) IMCUA(DivAssign, /) IMCUA(ModAssign, %)
    IMCUAL(AddAsLocal, +) IMCUAL(SubAsLocal, -) IMCUAL(MulAsLocal, *) IMCUAL(DivAsLocal, /) IMCUAL(ModAsLocal, %)
    
    INTERPRETER_MIDCASE(Goto)
        i = valpop().as_label().loc;
    INTERPRETER_MIDCASE(GotoLabel)
        i = n;
    
    INTERPRETER_MIDCASE(IntegerInline)
        valpush((int64_t)(iwordsigned_t)n);
    INTERPRETER_MIDCASE(IntegerInlineBigDec)
        valpush(((int64_t)(iwordsigned_t)n)*10000);
    INTERPRETER_MIDCASE(IntegerInlineBigBin)
        valpush(((int64_t)(iwordsigned_t)n)<<15);
    INTERPRETER_MIDCASE(Integer)
        valpush((int64_t)s.programdata.get_token_int(n));
    
    INTERPRETER_MIDCASE(DoubleInline)
        uint64_t dec = ((uint64_t)n) << iword_bits_from_i64;
        double d;
        memcpy(&d, &dec, sizeof(dec));
        valpush(d);
    INTERPRETER_MIDCASE(Double)
        valpush(s.programdata.get_token_double(n));
    
    INTERPRETER_MIDCASE(LocalVar)
        valpush(s.varstack_raw[n]);
    
    INTERPRETER_MIDCASE(GlobalVar)
        valpush(s.globals_raw[n]);
    
    INTERPRETER_MIDCASE(ArrayBuild)
        auto back = std::move(s.evalstack);
        s.evalstack = vec_pop_back(s.evalstacks);
        valpush(make_array(make_array_data(std::move(back))));
    
    INTERPRETER_MIDCASE(ArrayIndex) valreq(2);
        auto n = valpop().as_into_int();
        auto val = valpop();
        auto a = val.as_array_ptr_thru_ref();
        // FIXME use valback?
        if (val.is_array())
            valpush((*a->items()).at(n));
        else
            valpush({make_ref(a->items(), (size_t)n)});
    
    INTERPRETER_MIDCASE(Clone)
        auto & x = valback();
        x = x.clone(false);
    INTERPRETER_MIDCASE(CloneDeep)
        auto & x = valback();
        x = x.clone(true);
    
    INTERPRETER_MIDCASE(ArrayLen)
        auto & a = valback();
        a = ((int64_t)a.as_array_ptr_thru_ref()->items()->size());
    
    INTERPRETER_MIDCASE(ArrayLenMinusOne)
        auto & a = valback();
        a = ((int64_t)a.as_array_ptr_thru_ref()->items()->size() - 1);
    
    INTERPRETER_MIDCASE(ArrayPushIn) valreq(3);
        auto inval = valpop();
        auto n = valpop().as_into_int();
        auto v = valpop();
        Array * a = v.as_array_ptr_thru_ref();
        a->dirtify();
        a->items()->insert(a->items()->begin() + n, inval);
    
    INTERPRETER_MIDCASE(ArrayPopOut) valreq(2);
        auto n = valpop().as_into_int();
        auto v = valpop();
        Array * a = v.as_array_ptr_thru_ref();
        a->dirtify();
        auto ret = (*a->items()).at(n);
        a->items()->erase(a->items()->begin() + n);
        valpush(ret); // FIXME use valmap
    
    INTERPRETER_MIDCASE(ArrayConcat) valreq(2);
        auto vr = valpop();
        Array * ar = vr.as_array_ptr_thru_ref();
        auto vl = valpop();
        Array * al = vl.as_array_ptr_thru_ref();
        auto newarray = make_array(make_array_data());
        
        newarray.items()->insert(newarray.items()->end(), al->items()->begin(), al->items()->end());
        newarray.items()->insert(newarray.items()->end(), ar->items()->begin(), ar->items()->end());
        valpush(newarray); // FIXME use valmap
    
    INTERPRETER_MIDCASE(StringLiteral)
        valpush(make_array(make_array_data(s.programdata.get_token_stringval(n))));
    
    INTERPRETER_MIDCASE(StringLitReference)
        valpush(make_array(s.programdata.get_token_stringref(n)));
    
    INTERPRETER_MIDCASE(BuiltinCall)
        builtins[n](s.evalstack);
    
    INTERPRETER_MIDCASE(Punt)
        if (s.evalstacks.size() == 0) THROWSTR("Tried to punt when only one evaluation stack was open");
        s.evalstacks.back().push_back(valpop());
        
    INTERPRETER_MIDCASE(PuntN)
        if (s.evalstacks.size() == 0) THROWSTR("Tried to punt when only one evaluation stack was open");
        size_t count = valpop().as_into_int();
        for (size_t n = 0; n < count; n++)
            s.evalstacks.back().push_back(valpop());
        std::reverse(s.evalstacks.back().end() - count, s.evalstacks.back().end());
    
    INTERPRETER_MIDCASE(Exit)
        INTERPRETER_DOEXIT();
        
    INTERPRETER_ENDCASE()
    INTERPRETER_ENDDEF()
}

#if !defined(INTERPRETER_USE_LOOP) && !defined(INTERPRETER_USE_CGOTO)
#define PFX(X) Handler##X
const HandlerInfo handler = { TOKEN_TABLE };
#undef PFX
#endif

int interpret(const Program & programdata)
{
    interpreter_core(programdata, 0);
    return 0;
}

#endif // FLINCH_INCLUDE
