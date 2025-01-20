#ifndef FLINCH_INCLUDE
#define FLINCH_INCLUDE

#ifdef USE_MIMALLOC
#include <mimalloc-new-delete.h>
#endif

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

#include <cassert>

#include <initializer_list>

#ifndef NOINLINE
#define NOINLINE __attribute__((noinline))
#endif

#ifdef USE_MIMALLOC
#define malloc mi_malloc
#define realloc mi_realloc
#define free mi_free
#endif

//#define THROWSTR(X) throw std::runtime_error(X)
//#define THROWSTR(X) throw (X)
[[noreturn]] void THROWSTR(const char * X) { throw std::runtime_error(X); }
template <typename T> [[noreturn]] void THROWSTR(T X) { throw std::runtime_error(X.data()); }
//template <typename T> [[noreturn]] void THROWSTR(T X) { throw X; }
//template <typename T> [[noreturn]] void THROWSTR(T) { throw; }

template <typename T>
[[noreturn]] void rethrow(int line, int i, T & e)
{
    THROWSTR("Error on line " + std::to_string(line) + ": " + e.what() + " (token " + std::to_string(i) + ")");
}

#include "types.hpp"

template <typename T> T & vec_at_back(PODVec<T> & v)
{
    if (!v.size()) THROWSTR("tried to access empty buffer");
    return v.back();
}
template <typename T> T vec_pop_back(PODVec<T> & v)
{
    T ret = std::move(vec_at_back(v));
    v.pop_back();
    return ret;
}

#define erase_at(X, I) X.erase_at(I)
#define insert_at(X, I, V) X.insert_at(I, V)

using namespace std;

typedef uint32_t iword_t;
typedef  int32_t iwordsigned_t;
const int iword_bits_from_i64 = 8 * (sizeof(uint64_t)-sizeof(iword_t));

#define TOKEN_TABLE \
PFX(Exit),PFX(GlobalVar),PFX(GlobalVarDec),PFX(GlobalVarLookup),PFX(GlobalVarDecLookup),\
    PFX(LocalVar),PFX(LocalVarDec),PFX(LocalVarLookup),PFX(LocalVarDecLookup),PFX(Assign),PFX(AsLocal),\
PFX(Integer),PFX(IntegerInline),PFX(IntegerInlineBigDec),PFX(IntegerInlineBigBin),PFX(Double),PFX(DoubleInline),\
PFX(Add),PFX(Sub),PFX(Mul),PFX(Div),PFX(Mod),\
    PFX(AddAssign),PFX(SubAssign),PFX(MulAssign),PFX(DivAssign),PFX(ModAssign),\
    PFX(AddAsLocal),PFX(SubAsLocal),PFX(MulAsLocal),PFX(DivAsLocal),PFX(ModAsLocal),\
    PFX(AddIntInline),PFX(SubIntInline),PFX(MulIntInline),PFX(DivIntInline),PFX(ModIntInline),\
    PFX(AddDubInline),PFX(SubDubInline),PFX(MulDubInline),PFX(DivDubInline),PFX(ModDubInline),\
PFX(Neg),PFX(BitNot),PFX(And),PFX(Or),PFX(Xor),PFX(Shl),PFX(Shr),PFX(BoolNot),PFX(BoolAnd),PFX(BoolOr),\
PFX(ScopeOpen),PFX(ScopeClose),PFX(ArrayBuild),PFX(ArrayEmptyLit),PFX(Clone),PFX(CloneDeep),PFX(Punt),PFX(PuntN),\
PFX(ArrayIndex),PFX(ArrayLen),PFX(ArrayLenMinusOne),PFX(ArrayPushIn),PFX(ArrayPopOut),PFX(ArrayPushBack),PFX(ArrayPopBack),PFX(ArrayConcat),PFX(ArrayDelete),\
PFX(StringLiteral),PFX(StringLitReference),\
PFX(FuncDec),PFX(FuncLookup),PFX(FuncCall),PFX(FuncEnd),PFX(LabelDec),PFX(LabelLookup),\
PFX(Goto),PFX(GotoLabel),PFX(IfGoto),PFX(IfGotoLabel),\
    PFX(IfGotoLabelEQ),PFX(IfGotoLabelNE),PFX(IfGotoLabelLE),PFX(IfGotoLabelGE),PFX(IfGotoLabelLT),PFX(IfGotoLabelGT),\
    PFX(        CmpEQ),PFX(        CmpNE),PFX(        CmpLE),PFX(        CmpGE),PFX(        CmpLT),PFX(        CmpGT),\
PFX(ForLoop),PFX(ForLoopLabel),PFX(ForLoopLocal),\
PFX(Call),PFX(BuiltinCall),PFX(Return)

// token kind
#define PFX(X) X
enum TKind : iword_t {
    TOKEN_TABLE,
    HandlerCount
};
#undef PFX

#define PFX(X) { (X) , (#X) }
PODListMap<TKind, const char *> tnames = { TOKEN_TABLE };
#undef PFX

//struct Token { TKind kind; iword_t n, extra_1, extra_2; };
struct Token { iword_t kind, n, extra_1, extra_2; };
struct CompFunc { iword_t loc, len, varcount; };
struct Func { iword_t loc, varcount; };
Token make_token(TKind kind, iword_t n) { return {kind, n, 0, 0}; }

struct DynamicType;
typedef PODVec<DynamicType> * ArrayData;

struct Ref {
    DynamicType * info;
    DynamicType * ref() { return info; }
};

#define MAKEREF return Ref{&items->at(i)};
#define MAKEREF2 return Ref{items->data() + i};

struct Label { int loc; };
struct Array {
    ArrayData info;
    ArrayData & items();
};

ArrayData & Array::items() { return info; }
Array make_array(ArrayData backing) { return Array { backing }; }

// DynamicType can hold any of these types
struct DynamicType {
    variant<int64_t, double, Label, Func, Ref, Array> value;

    DynamicType() : value((int64_t)0) { }
    DynamicType(int64_t v) : value((int64_t)v) { }
    DynamicType(int v) : value((int64_t)v) { }
    DynamicType(short v) : value((int64_t)v) { }
    DynamicType(char v) : value((int64_t)v) { }
    DynamicType(double v) : value((double)v) { }
    DynamicType(const Label & l) : value(l) { }
    DynamicType(const Func & f) : value(f) { }
    DynamicType(const Array & a) : value(a) { }
    DynamicType(const Ref & v) : value(v) { }
    DynamicType(Array && a) : value(a) { }
    DynamicType(Ref && v) : value(v) { }

    DynamicType(const DynamicType & other) = default;
    DynamicType(DynamicType && other) noexcept = default;
    DynamicType& operator=(const DynamicType & other) = default;
    DynamicType& operator=(DynamicType && other) noexcept = default;
    
    #define INFIX(WRAPPER1, WRAPPER2, OP, OP2, WX)\
        if (holds_alternative<int64_t>(value) && holds_alternative<int64_t>(other.value))\
            return WRAPPER1(WX(std::get<int64_t>(value)) OP WX(std::get<int64_t>(other.value)));\
        else if (holds_alternative<double>(value) && holds_alternative<double>(other.value))\
            return WRAPPER2(WX(std::get<double>(value)) OP2 WX(std::get<double>(other.value)));\
        else if (holds_alternative<int64_t>(value) && holds_alternative<double>(other.value))\
            return WRAPPER2(WX(std::get<int64_t>(value)) OP2 WX(std::get<double>(other.value)));\
        else if (holds_alternative<double>(value) && holds_alternative<int64_t>(other.value))\
            return WRAPPER2(WX(std::get<double>(value)) OP2 WX(std::get<int64_t>(other.value)));\
        else THROWSTR("Unsupported operation: non-numeric operands for operator " #OP);
    
    #define COMMA ,
    
    DynamicType operator+(const DynamicType& other) const { INFIX(DynamicType, DynamicType, +, +, ) }
    DynamicType operator-(const DynamicType& other) const { INFIX(DynamicType, DynamicType, -, -, ) }
    DynamicType operator*(const DynamicType& other) const { INFIX(DynamicType, DynamicType, *, *, ) }
    DynamicType operator/(const DynamicType& other) const { INFIX(DynamicType, DynamicType, /, /, ) }
    DynamicType operator%(const DynamicType& other) const { INFIX(DynamicType, fmod, %, COMMA, ) }
    
    bool operator==(const DynamicType& other) const { INFIX(!!, !!, ==, ==, ) }
    bool operator!=(const DynamicType& other) const { INFIX(!!, !!, !=, !=, ) }
    bool operator>=(const DynamicType& other) const { INFIX(!!, !!, >=, >=, ) }
    bool operator<=(const DynamicType& other) const { INFIX(!!, !!, <=, <=, ) }
    bool operator> (const DynamicType& other) const { INFIX(!!, !!,  >,  >, ) }
    bool operator< (const DynamicType& other) const { INFIX(!!, !!,  <,  <, ) }
    
    DynamicType operator<<(const DynamicType& other) const { INFIX(int64_t, int64_t, <<, <<, int64_t) }
    DynamicType operator>>(const DynamicType& other) const { INFIX(int64_t, int64_t, >>, >>, int64_t) }
    
    DynamicType operator&(const DynamicType& other) const { INFIX(int64_t, int64_t, &, &, uint64_t) }
    DynamicType operator|(const DynamicType& other) const { INFIX(int64_t, int64_t, |, |, uint64_t) }
    DynamicType operator^(const DynamicType& other) const { INFIX(int64_t, int64_t, ^, ^, uint64_t) }
    
    bool operator&&(const DynamicType& other) const { INFIX(!!, !!, &&, &&, ) }
    bool operator||(const DynamicType& other) const { INFIX(!!, !!, ||, ||, ) }
    
    #define UNARY(WRAPPER, OP, WX)\
        if (holds_alternative<int64_t>(value))\
            return WRAPPER(OP WX(std::get<int64_t>(value)));\
        else if (holds_alternative<double>(value))\
            return WRAPPER(OP WX(std::get<double>(value)));\
        else THROWSTR("Unsupported operation: non-numeric operands for operator " #OP);
    
    DynamicType operator-() const { UNARY(int64_t, -, ) }
    DynamicType operator!() const { UNARY(int64_t, !, ) }
    DynamicType operator~() const { UNARY(int64_t, ~, uint64_t) }
    
    #define AS_TYPE_X(TYPE, TYPENAME)\
    TYPE & as_##TYPENAME()\
    {\
        if (holds_alternative<TYPE>(value)) return std::get<TYPE>(value);\
        THROWSTR("Value is not of type " #TYPENAME);\
    }
    
    AS_TYPE_X(int64_t, int)
    AS_TYPE_X(double, double)
    AS_TYPE_X(Ref, ref)
    AS_TYPE_X(Label, label)
    AS_TYPE_X(Func, func)
    AS_TYPE_X(Array, array)
    
    bool is_int() { return holds_alternative<int64_t>(value); }
    bool is_double() { return holds_alternative<double>(value); }
    bool is_ref() { return holds_alternative<Ref>(value); }
    bool is_label() { return holds_alternative<Label>(value); }
    bool is_func() { return holds_alternative<Func>(value); }
    bool is_array() { return holds_alternative<Array>(value); }
    
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
        if (holds_alternative<int64_t>(value)) return !!std::get<int64_t>(value);
        else if (holds_alternative<double>(value)) return !!std::get<double>(value);
        return true;
    }
    
    DynamicType clone(bool deep);
};

ArrayData make_array_data(PODVec<DynamicType> x) { return new PODVec<DynamicType>(std::move(x)); }
ArrayData make_array_data() { return new PODVec<DynamicType>(); }
    
DynamicType DynamicType::clone(bool deep)
{
    if (is_ref()) return *as_ref().ref();
    else if (!is_array()) return *this;
    auto n = make_array(make_array_data(*as_array().items()));
    if (!deep) return n;
    for (auto & item : *n.items()) item = item.clone(deep);
    return n;
}

inline Ref make_ref(ArrayData & items, size_t i) { MAKEREF }
inline Ref make_ref_2(ArrayData & items, size_t i) { MAKEREF2 }

struct Program {
    PODVec<Token> program;
    PODVec<int> lines;
    PODVec<CompFunc> funcs;
    
    #define TOKEN_LOG(NAME, TYPE, EXTRA, EXTRA2)\
    PODVec<TYPE> token_##NAME##s;\
    iword_t get_token_##NAME##_num(TYPE s)\
    {\
        for (iword_t i = 0; i < token_##NAME##s.size(); i++) { if (token_##NAME##s[i] == s) { EXTRA; return i; } }\
        token_##NAME##s.push_back(s);\
        EXTRA2;\
        return token_##NAME##s.size() - 1;\
    }\
    TYPE get_token_##NAME(iword_t n) const { return token_##NAME##s[n]; }
    
    TOKEN_LOG(string, ShortString,,)
    TOKEN_LOG(func, ShortString,,)
    TOKEN_LOG(varname, ShortString,,)
    TOKEN_LOG(int, int64_t,,)
    TOKEN_LOG(double, double,,)
    TOKEN_LOG(stringval, PODVec<DynamicType>,,)
    TOKEN_LOG(stringref, ArrayData,delete s,)
    
    ~Program()
    {
        for (auto & p : token_stringrefs)
            delete p;
    }
};

// built-in function definitions. must be specifically here. do not move.
#include "builtins.hpp"

Program load_program(string text)
{
    size_t line = 0;
    size_t i = 0;
    
    Program programdata;
    PODVec<ShortString> program_texts;
    
    auto & p = programdata.program;
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
                    THROWSTR("Char literal must be a single char or a \\ followed by a single char on line " + to_string(line));
            }
            else if (text[i] == '"')
            {
                i++;
                ShortString s = "\"";
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
                i += 1;
                if (i < text.size() && text[i] == '*') s += text[i++];
                
                if (i < text.size() && !isspace(text[i]) && text[i] != 0)
                    THROWSTR("String literals must not have trailing text after the closing \" character");
            }
            else
            {
                while (i < text.size() && !isspace(text[i])) i++;
            }
            program_texts.push_back(text.substr(start_i, i - start_i).data());
            lines.push_back(line + 1);
        }
    }
    
    //lines.push_back(lines.back());
    
    
    //for (auto & n : program_texts)
    //    printf("``%s``\n", n.data());
    
    //for (auto & n : lines)
    //    printf("%d\n", n);
    //printf("(%d)\n", lines.size());
    
    auto isint = [&](const ShortString& str) {
        if (str.empty()) return false;
        for (size_t i = (str[0] == '+' || str[0] == '-'); i < str.length(); i++) { if (!std::isdigit(str[i])) return false; }
        return true;
    };
    auto isfloat = [&](const ShortString& str) {
        if (isint(str)) return false;
        try { stod(str.data()); return true; }
        catch (...) { }
        return false;
    };
    
    auto isname = [&](const ShortString& str) {
        if (str.empty() || (!isalpha(str[0]) && str[0] != '_')) return false;
        for (size_t i = 1; i < str.size(); i++) { if (!isalnum(str[i]) && str[i] != '_') return false; }
        return true;
    };
    
    programdata.get_token_func_num("");
    
    PODVec<PODVec<ShortString>> var_defs;
    var_defs.push_back({});
    
    auto var_is_local = [&](ShortString & s) {
        if (var_defs.size() == 1) return false;
        return std::find(var_defs.back().begin(), var_defs.back().end(), s) != var_defs.back().end();
    };
    
    auto var_is_global = [&](ShortString & s) {
        return std::find(var_defs[0].begin(), var_defs[0].end(), s) != var_defs[0].end();
    };
    
    auto shunting_yard = [&](size_t i) {
        size_t start_i = i;
        erase_at(program_texts, i); // erase leading paren
        erase_at(lines, i);
        
        PODListMap<ShortString, int> prec;
        
        #define ADD_PREC(N, X) for (auto & s : X) prec.insert(s, N);
        ADD_PREC(6, (initializer_list<ShortString>{ "@", "@-", "@--", "@+", "@++", "@@" }));
        ADD_PREC(5, (initializer_list<ShortString>{ "*", "/", "%", "<<", ">>", "&" }));
        ADD_PREC(4, (initializer_list<ShortString>{ "+", "-", "|", "^" }));
        ADD_PREC(3, (initializer_list<ShortString>{ "==", "<=", ">=", "!=", ">", "<" }));
        ADD_PREC(2, (initializer_list<ShortString>{ "and", "or" }));
        ADD_PREC(1, (initializer_list<ShortString>{ "->", "+=", "-=", "*=", "/=", "%=" }));
        ADD_PREC(0, (initializer_list<ShortString>{ ";" }));
        
        PODVec<int> nums, ops;
        
        while (i < program_texts.size() && program_texts[i] != ")")
        {
            if (program_texts[i] == ("(" )|| program_texts[i] == ("(("))
            {
                for (int x = 0; i < program_texts.size() && (x || program_texts[i] == ("(") || program_texts[i] == ("((")); i++)
                {
                    x += (program_texts[i] == ("(" )|| program_texts[i] == ("((")) - (program_texts[i] == (")") || program_texts[i] == ("))"));
                    nums.push_back(i);
                }
            }
            else if (!prec.count(program_texts[i]))
                nums.push_back(i++);
            else
            {
                //while (ops.size() && prec[program_texts[i]] < prec[program_texts[ops.back()]])
                while (ops.size() && prec[program_texts[i]] <= prec[program_texts[ops.back()]])
                    nums.push_back(vec_pop_back(ops));
                ops.push_back(i++);
            }
        }
        while (ops.size()) nums.push_back(vec_pop_back(ops));
        
        if (i >= program_texts.size())
            THROWSTR("Paren expression must end in a closing paren, i.e. ')', starting on or near line " + to_string(lines[start_i]));
        
        // parallel move; we go through this effort to keep lines and texts in sync
        PODVec<ShortString> texts_s;
        PODVec<int> lines_s;
        for (size_t j = 0; j < nums.size(); j++)
        {
            texts_s.push_back(std::move(program_texts[nums[j]]));
            lines_s.push_back(lines[nums[j]]);
        }
        for (size_t j = 0; j < nums.size(); j++)
        {
            program_texts[j + start_i] = std::move(texts_s[j]);
            lines[j + start_i] = lines_s[j];
        }
        
        erase_at(program_texts, i);
        erase_at(lines, i);
    };
    
    PODListMap<ShortString, TKind> trivial_ops;
    
    trivial_ops.insert("call", Call);
    trivial_ops.insert("if_goto", IfGoto);
    trivial_ops.insert("goto", Goto);
    trivial_ops.insert("inc_goto_until", ForLoop);
    trivial_ops.insert("punt", Punt);
    trivial_ops.insert("punt_n", PuntN);
    
    trivial_ops.insert("->", Assign);
    
    trivial_ops.insert("[", ScopeOpen);
    trivial_ops.insert("]~", ScopeClose);
    trivial_ops.insert("]", ArrayBuild);
    trivial_ops.insert("[]", ArrayEmptyLit);
    
    trivial_ops.insert("+", Add);
    trivial_ops.insert("-", Sub);
    trivial_ops.insert("*", Mul);
    trivial_ops.insert("/", Div);
    trivial_ops.insert("%", Mod);
    
    trivial_ops.insert("+=", AddAssign);
    trivial_ops.insert("-=", SubAssign);
    trivial_ops.insert("*=", MulAssign);
    trivial_ops.insert("/=", DivAssign);
    trivial_ops.insert("%=", ModAssign);
    
    trivial_ops.insert("&", And);
    trivial_ops.insert("|", Or);
    trivial_ops.insert("^", Xor);
    trivial_ops.insert("<<", Shl);
    trivial_ops.insert(">>", Shr);
    
    trivial_ops.insert("neg", Neg);
    trivial_ops.insert("~", BitNot);
    trivial_ops.insert("!", BoolNot);
    
    trivial_ops.insert("and", BoolAnd);
    trivial_ops.insert("or", BoolOr);
    
    trivial_ops.insert("==", CmpEQ);
    trivial_ops.insert("!=", CmpNE);
    trivial_ops.insert("<=", CmpLE);
    trivial_ops.insert(">=", CmpGE);
    trivial_ops.insert("<",  CmpLT);
    trivial_ops.insert(">",  CmpGT);
    
    trivial_ops.insert("::", Clone);
    trivial_ops.insert("::!", CloneDeep);
    
    trivial_ops.insert("@", ArrayIndex);
    trivial_ops.insert("@?", ArrayLen);
    trivial_ops.insert("@?-", ArrayLenMinusOne);
    trivial_ops.insert("@+", ArrayPushIn);
    trivial_ops.insert("@-", ArrayPopOut);
    trivial_ops.insert("@++", ArrayPushBack);
    trivial_ops.insert("@--", ArrayPopBack);
    trivial_ops.insert("@@", ArrayConcat);
    trivial_ops.insert("@~", ArrayDelete);
    
    for (i = 0; i < program_texts.size() && program_texts[i] != ""; i++)
    {
        ShortString & token = program_texts[i];

        if (token == "(")
            shunting_yard(i--);
        else if (token == "((" || token == "))" || token == ";")
            erase_at(lines, i);
        else if (token != "^^" && token.size() >= 2 && token.back() == '^')
        {
            var_defs.push_back({});
            p.push_back(make_token(FuncDec, programdata.get_token_func_num(token.substr(0, token.size() - 1))));
        }
        else if (token.size() >= 2 && token.front() == '.')
            p.push_back(make_token(FuncCall, programdata.get_token_func_num(token.substr(1))));
        else if (token != "^^" && token.size() >= 2 && token.front() == '^')
            p.push_back(make_token(FuncLookup, programdata.get_token_func_num(token.substr(1))));
        else if (token == "^^")
        {
            var_defs.back().clear();
            var_defs.pop_back();
            p.push_back(make_token(FuncEnd, 0));
        }
        else if (token == "return")
            p.push_back(make_token(Return, 0));
        else if (token.front() == '$' && token.back() == '$' && token.size() >= 3)
        {
            auto s = token.substr(1, token.size() - 2);
            var_defs.back().push_back(s);
            auto n = programdata.get_token_varname_num(s);
            if (var_defs.size() > 1)
                p.push_back(make_token(LocalVarDecLookup, n));
            else
                p.push_back(make_token(GlobalVarDecLookup, n));
        }
        else if (token.back() == '$' && token.size() >= 2)
        {
            auto s = token.substr(0, token.size() - 1);
            auto n = programdata.get_token_varname_num(s);
            var_defs.back().push_back(s);
            if (var_defs.size() > 1)
                p.push_back(make_token(LocalVarDec, n));
            else
                p.push_back(make_token(GlobalVarDec, n));
        }
        else if (token.front() == '$' && token.size() >= 2)
        {
            auto s = token.substr(1);
            auto n = programdata.get_token_varname_num(s);
            if (var_is_local(s))
                p.push_back(make_token(LocalVarLookup, n));
            else if (var_is_global(s))
                p.push_back(make_token(GlobalVarLookup, n));
            else
                THROWSTR("Undefined variable " + s + " on line " + to_string(lines[i]).data());
        }
        else if (token.front() == ':' && token.size() >= 2 && token != "::" && token != "::!")
            p.push_back(make_token(LabelLookup, programdata.get_token_string_num(token.substr(1))));
        else if (token.back() == ':' && token.size() >= 2 && token != "::" && token != "::!")
            p.push_back(make_token(LabelDec, programdata.get_token_string_num(token.substr(0, token.size() - 1))));
        else if (trivial_ops.count(token))
        {
            p.push_back(make_token(trivial_ops[token], 0));
        }
        else if (token.size() >= 3 && token[0] == '"' && token.back() == '*') // '"' (fix tokei line counts)
        {
            PODVec<DynamicType> str = {};
            for (size_t i = 1; i < token.size() - 2; i++)
                str.push_back((int64_t)token[i]);
            auto n = programdata.get_token_stringval_num(std::move(str));
            p.push_back(make_token(StringLiteral, n));
        }
        else if (token.size() >= 2 && token[0] == '"' && token.back() == '"')
        {
            auto str = make_array_data();
            for (size_t i = 1; i < token.size() - 1; i++)
                str->push_back((int64_t)token[i]);
            auto n = programdata.get_token_stringref_num(str);
            p.push_back(make_token(StringLitReference, n));
        }
        else if (token.size() >= 2 && token[0] == '!')
        {
            auto s = token.substr(1);
            p.push_back(make_token(BuiltinCall, builtins_lookup(s)));
        }
        else if ((token.size() == 3 || token.size() == 4) && token[0] == '\'' && token.back() == '\'')
        {
            if (token.size() == 3) p.push_back(make_token(IntegerInline, token[1]));
            else if (token.size() != 4 || token[1] != '\\') THROWSTR("Char literal must be a single char or a \\ followed by a single char on line " + to_string(lines[i]));
            else if (token[2] == 'n') p.push_back(make_token(IntegerInline, '\n'));
            else if (token[2] == 'r') p.push_back(make_token(IntegerInline, '\r'));
            else if (token[2] == 't') p.push_back(make_token(IntegerInline, '\t'));
            else if (token[2] == '\\') p.push_back(make_token(IntegerInline, '\\'));
            else p.push_back(make_token(IntegerInline, token[2]));
        }
        else
        {
            if (isint(token))
            {
                int sbits = (sizeof(iword_t)*8-1);
                int64_t num = stoll(token.data());
                int64_t num2_smol = num  / (sbits == 15 ? 10000 : sbits == 31 ? 1000000000 : sbits == 63 ? 1 : 50);
                int64_t num2 = num2_smol * (sbits == 15 ? 10000 : sbits == 31 ? 1000000000 : sbits == 63 ? 1 : 50);
                int64_t num3_smol = num >> sbits;
                int64_t num3 = (uint64_t)num3_smol << sbits;
                
                int64_t d = (int64_t)1 << sbits;
                int64_t dhi = d-1;
                int64_t dlo = -d;
                
                if (num >= dlo && num <= dhi)
                    p.push_back(make_token(IntegerInline, (iword_t)num));
                else if (num2 == num && num2_smol >= dlo && num2_smol <= dhi)
                    p.push_back(make_token(IntegerInlineBigDec, (iword_t)num2_smol));
                else if (num3 == num && num3_smol >= dlo && num3_smol <= dhi)
                    p.push_back(make_token(IntegerInlineBigBin, (iword_t)num3_smol));
                else
                    p.push_back(make_token(Integer, programdata.get_token_int_num(num)));
            }
            else if (isfloat(token))
            {
                auto d = stod(token.data());
                
                uint64_t dec;
                memcpy(&dec, &d, sizeof(d));
                
                if ((dec >> iword_bits_from_i64) << iword_bits_from_i64 == dec)
                    p.push_back(make_token(DoubleInline, (iword_t)(dec >> iword_bits_from_i64)));
                else
                    p.push_back(make_token(Double, programdata.get_token_double_num(d)));
            }
            else if (isname(token))
            {
                auto n = programdata.get_token_varname_num(token);
                if (var_is_local(token) || var_is_global(token))
                    p.push_back(make_token(var_is_local(token) ? LocalVar : GlobalVar, n));
                else
                    THROWSTR("Undefined variable " + token + " on line " + to_string(lines[i]).data());
            }
            else
                THROWSTR("Invalid token: " + token);
        }
    }
    p.push_back(make_token(Exit, 0));
    
    //for (auto & s : program_texts)
    //    printf("%s\n", s.data());
    
    auto prog_erase = [&](auto i) -> auto {
        erase_at(p, i);
        erase_at(lines, i);
    };

    auto still_valid = [&]() { return i < p.size() && i + 1 < p.size() && p[i].kind != Exit && p[i + 1].kind != Exit; };

    // peephole optimizer!
    for (i = 0; ((ptrdiff_t)i) < 0 || (i < p.size() && p[i].kind != Exit && p[i + 1].kind != Exit); ++i)
    {
        if (still_valid() && p[i].kind == IntegerInline && (p[i+1].kind == Add || p[i+1].kind == Sub ||
            p[i+1].kind == Mul || p[i+1].kind == Div || p[i+1].kind == Mod))
        {
            p[i].kind = (TKind)(p[i+1].kind + (AddIntInline - Add));
            prog_erase(i-- + 1);
        }
        if (still_valid() && p[i].kind == DoubleInline && (p[i+1].kind == Add || p[i+1].kind == Sub ||
            p[i+1].kind == Mul || p[i+1].kind == Div || p[i+1].kind == Mod))
        {
            p[i].kind = (TKind)(p[i+1].kind + (AddDubInline - Add));
            prog_erase(i-- + 1);
        }
        if (still_valid() && p[i].kind == LocalVarLookup && (p[i+1].kind == AddAssign || p[i+1].kind == SubAssign ||
             p[i+1].kind == MulAssign || p[i+1].kind == DivAssign || p[i+1].kind == ModAssign))
        {
            p[i].kind = (TKind)(p[i+1].kind + (AddAsLocal - AddAssign));
            prog_erase(i-- + 1);
        }
        if (still_valid() && p[i].kind == LocalVarLookup &&
            (p[i+1].kind == Assign || p[i+1].kind == Goto || p[i+1].kind == IfGoto))
        {
            p[i].kind = p[i+1].kind == Assign ? AsLocal : p[i+1].kind == Goto ? GotoLabel : IfGotoLabel;
            prog_erase(i-- + 1);
        }
        if (still_valid() && p[i].kind == LabelLookup && p[i+1].kind == ForLoop)
        {
            p[i].kind = ForLoopLabel;
            prog_erase(i-- + 1);
            // need to be able to see the output of this optimization one token to the left, for the next optimization
            i--;
        }
        if (still_valid() && i + 2 < p.size() && p[i].kind == LocalVarLookup && p[i+1].kind == IntegerInline && p[i+2].kind == ForLoopLabel)
        {
            p[i].kind = ForLoopLocal;
            p[i].extra_1 = p[i].n;
            p[i].extra_2 = p[i+1].n;
            p[i].n = p[i+2].n;
            prog_erase(i + 2);
            prog_erase(i-- + 1);
        }
        if (still_valid() && p[i].kind >= CmpEQ && p[i].kind <= CmpGT && p[i+1].kind == IfGotoLabel)
        {
            p[i].kind = TKind(IfGotoLabelEQ + (p[i].kind - CmpEQ));
            p[i].n = p[i+1].n;
            prog_erase(i-- + 1);
        }
    }

    funcs = PODVec<CompFunc>(programdata.token_funcs.size());
    std::fill(funcs.begin(), funcs.end(), CompFunc{0,0,0});
    
    PODVec<iword_t> root_labels(programdata.token_strings.size());
    std::fill(root_labels.begin(), root_labels.end(), (iword_t)-1);
    
    // rewrite in-function labels, register functions
    // also compactify in-function variables
    // also rewrite root-level labels
    for (i = 0; i < p.size() && p[i].kind != Exit; i++)
    {
        if (p[i].kind == FuncDec)
        {
            if (funcs[p[i].n].len != 0)
                THROWSTR("Redefined function on or near line " + to_string(lines[i]));
            
            PODVec<iword_t> labels(programdata.token_strings.size());
            std::fill(labels.begin(), labels.end(), (iword_t)-1);
            
            PODListMap<iword_t, iword_t> varnames_set;
            iword_t vn = 0;
            for (size_t i2 = i + 1; p[i2].kind != FuncEnd; i2 += 1)
            {
                if (p[i2].kind == LabelDec)
                {
                    labels[p[i2].n] = (iword_t)i2;
                    prog_erase(i2--);
                }
                if ((p[i2].kind == LocalVarDec || p[i2].kind == LocalVarDecLookup) && !varnames_set.count(p[i2].n))
                    varnames_set.insert(p[i2].n, vn++);
            }
            
            // this needs to be a separate pass because labels can be seen upwards, not just downwards
            size_t i2 = i + 1;
            for (; p[i2].kind != FuncEnd; i2 += 1)
            {
                if (p[i2].kind == LocalVarDec || p[i2].kind == LocalVarDecLookup || p[i2].kind == LocalVarLookup ||
                    p[i2].kind == LocalVar || p[i2].kind == AsLocal ||p[i2].kind == AddAsLocal || p[i2].kind == SubAsLocal ||
                    p[i2].kind == MulAsLocal || p[i2].kind == DivAsLocal || p[i2].kind == ModAsLocal)
                    p[i2].n = varnames_set[p[i2].n];
                
                if (p[i2].kind == ForLoopLocal)
                    p[i2].extra_1 = varnames_set[p[i2].extra_1];
                
                if (p[i2].kind == LabelLookup || p[i2].kind == GotoLabel || p[i2].kind == ForLoopLabel ||
                    p[i2].kind == ForLoopLocal || (p[i2].kind >= IfGotoLabel && p[i2].kind <= IfGotoLabelGT))
                {
                    p[i2].n = labels[p[i2].n];
                    if(p[i2].n == (iword_t)-1)
                        THROWSTR("Unknown label usage on or near line " + to_string(lines[i2]));
                }
            }
            if (i2 - i >= (size_t)iword_t(-1)) THROWSTR("Single functions contains far too many operations");
            if (vn >= (size_t)iword_t(-1))     THROWSTR("Single functions contains far too many variables");
            funcs[p[i].n] = CompFunc{(iword_t)(i + 1), (iword_t)(i2 - i), (iword_t)vn};
            
            i = i2;
        }
        else if (p[i].kind == LabelDec)
        {
            root_labels[p[i].n] = (iword_t)i;
            prog_erase(i--);
        }
    }
    
    // rewrite global labels
    // this needs to be a separate pass because labels can be seen upwards, not just downwards
    for (i = 0; i < p.size() && p[i].kind != Exit; i++)
    {
        if (p[i].kind == FuncDec) i += funcs[p[i].n].len;
        
        if (p[i].kind == LabelLookup || p[i].kind == GotoLabel || p[i].kind == ForLoopLabel ||
            p[i].kind == ForLoopLocal || (p[i].kind >= IfGotoLabel && p[i].kind <= IfGotoLabelGT))
        {
            p[i].n = root_labels[p[i].n];
            if (p[i].n == (iword_t)-1)
                THROWSTR("Unknown label usage on or near line " + to_string(lines[i]));
        }
    }
    
    // disassembler
    //for (iword_t i = 0; i < p.size(); i++)
    //    printf("%u \t: %s\t%u\t%u\t%u\n", i, tnames[(TKind)p[i].kind], p[i].n, p[i].extra_1, p[i].extra_2);
    
    for (auto & n : var_defs)
        n.clear();
    
    return programdata;
}

struct ProgramState {
    const Program & programdata;
    const PODVec<CompFunc> & funcs;
    PODVec<DynamicType> vars_default;
    PODVec<iword_t> callstack;
    
    ArrayData globals;
    DynamicType * globals_raw;
    
    PODVec<ArrayData> varstacks;
    ArrayData varstack;
    DynamicType * varstack_raw;
    
    PODVec<PODVec<DynamicType>> evalstacks;
    PODVec<DynamicType> evalstack;
    
    ~ProgramState()
    {
        for (auto & n : varstacks)
        {
            n->clear();
            delete n;
        }
        for (auto & n : evalstacks)
            n.clear();
        evalstacks.clear();
        evalstack.clear();
        globals->clear();
        delete globals;
        varstack->clear();
        delete varstack;
    }
};

#if !defined(INTERPRETER_USE_LOOP) && !defined(INTERPRETER_USE_CGOTO)
typedef void(*[[clang::preserve_none]] HandlerT)(ProgramState & s, int i, const Token * program);
struct HandlerInfo { const HandlerT s[HandlerCount]; };
extern const HandlerInfo handler;
#endif

int interpreter_core(const Program & programdata, int i)
{
    auto program = programdata.program.data();
    
    PODVec<DynamicType> vars_default;
    for (size_t i = 0; i < programdata.token_varnames.size(); i++) vars_default.push_back(0);
    
    auto s = ProgramState {
        programdata, programdata.funcs, vars_default, {},
        
        make_array_data(vars_default),
        0,
        
        {},
        make_array_data(vars_default),
        0,
        
        {},
        {}
    };
    
    s.globals_raw = s.globals->data();
    s.varstack_raw = s.varstack->data();
    
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
        catch (const exception& e) { rethrow(s.programdata.lines[i-1], i-1, e); }
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
        catch (const exception& e) { rethrow(s.programdata.lines[i-1], i-1, e); }
    #define INTERPRETER_DOEXIT() goto INTERPRETER_EXIT;
    
    #else // of ifdef INTERPRETER_USE_LOOP
    
    #define INTERPRETER_NEXT() { [[clang::musttail]] return handler.s[program[i].kind](s, i, program); }
    #define INTERPRETER_DEF() { handler.s[program[i].kind](s, i, program); return 0; } }
    
    #define INTERPRETER_CASE(NAME)\
        extern "C" [[clang::preserve_none]] void Handler##NAME(ProgramState & s, int i, const Token * program) { \
        auto n = program[i++].n; (void)n; try {
        //printf("at %d in %s\n", i - 1, #NAME);
    #define INTERPRETER_ENDCASE() } catch (const exception& e) { rethrow(s.programdata.lines[i-1], i-1, e); }\
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
        valpush((Func{s.funcs[n].loc, s.funcs[n].varcount}));
    
    INTERPRETER_MIDCASE(FuncEnd)
        s.varstack->clear();
        delete s.varstack;
        s.varstack = vec_pop_back(s.varstacks);
        s.varstack_raw = s.varstack->data();
        i = vec_pop_back(s.callstack);
    INTERPRETER_MIDCASE(Return)
        s.varstack->clear();
        delete s.varstack;
        s.varstack = vec_pop_back(s.varstacks);
        s.varstack_raw = s.varstack->data();
        i = vec_pop_back(s.callstack);
    
    INTERPRETER_MIDCASE(LocalVarDec)
        s.varstack_raw[n] = 0;
    INTERPRETER_MIDCASE(LocalVarLookup)
        valpush(make_ref_2(s.varstack, n));
    INTERPRETER_MIDCASE(LocalVarDecLookup)
        s.varstack_raw[n] = 0;
        valpush(make_ref_2(s.varstack, n));
    INTERPRETER_MIDCASE(GlobalVarDec)
        s.globals_raw[n] = 0;
    INTERPRETER_MIDCASE(GlobalVarLookup)
        valpush(make_ref_2(s.globals, n));
    INTERPRETER_MIDCASE(GlobalVarDecLookup)
        s.globals_raw[n] = 0;
        valpush(make_ref_2(s.globals, n));
    
    INTERPRETER_MIDCASE(LabelLookup)
        valpush(Label{(int)n});
    INTERPRETER_MIDCASE(LabelDec)
        THROWSTR("internal interpreter error: tried to execute opcode that's supposed to be deleted");
        
    #define DO_FCALL()\
        s.callstack.push_back(i);\
        i = f.loc;\
        s.varstacks.push_back(std::move(s.varstack));\
        s.varstack = make_array_data(PODVec(f.varcount, DynamicType(0)));\
        s.varstack_raw = s.varstack->data();
    
    INTERPRETER_MIDCASE(Call)
        Func f = valpop().as_func();
        DO_FCALL()
    
    INTERPRETER_MIDCASE(FuncCall)
        Func f = {s.funcs[program[i-1].n].loc, s.funcs[program[i-1].n].varcount};
        DO_FCALL()
    
    INTERPRETER_MIDCASE(Assign) valreq(2);
        Ref ref = valpop().as_ref();
        auto x = valpop();
        *ref.ref() = std::move(x);
    
    INTERPRETER_MIDCASE(AsLocal)
        s.varstack_raw[n] = valpop();
    
    INTERPRETER_MIDCASE(ScopeOpen)
        s.evalstacks.push_back(std::move(s.evalstack));
        s.evalstack = {};
    INTERPRETER_MIDCASE(ScopeClose)
        s.evalstack = vec_pop_back(s.evalstacks);
    
    INTERPRETER_MIDCASE(IfGoto) valreq(2);
        Label dest = valpop().as_label();
        if (valpop()) i = dest.loc;
    INTERPRETER_MIDCASE(IfGotoLabel)
        if (valpop()) i = n;
    
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
    
    // INTERPRETER_MIDCASE_BINARY_SIMPLE
    #define IMCBS(NAME, OP) \
    INTERPRETER_MIDCASE(NAME) valreq(2);\
        auto b = valpop();\
        auto & x = valback();\
        x = x OP b;
    
    // INTERPRETER_MIDCASE_BINARY_INTINLINE
    #define IMCBII(NAME, OP) \
    INTERPRETER_MIDCASE(NAME);\
        auto b = (int64_t)(iwordsigned_t)n;\
        auto & x = valback();\
        x = x OP b;
    
    // INTERPRETER_MIDCASE_BINARY_DUBINLINE
    #define IMCBDI(NAME, OP) \
    INTERPRETER_MIDCASE(NAME)\
        uint64_t dec = ((uint64_t)n) << iword_bits_from_i64;\
        double d;\
        memcpy(&d, &dec, sizeof(dec));\
        auto & x = valback();\
        x = x OP d;
    
    // INTERPRETER_MIDCASE_BINARY_ASSIGN
    #define IMCBA(NAME, OP) \
    INTERPRETER_MIDCASE(NAME) valreq(2);\
        Ref ref = std::move(valpop().as_ref());\
        auto a = valpop();\
        *ref.ref() = *ref.ref() OP a;
    
    // INTERPRETER_MIDCASE_BINARY_ASSIGNLOC
    #define IMCBAL(NAME, OP) \
    INTERPRETER_MIDCASE(NAME)\
        auto a = valpop();\
        s.varstack_raw[n] = s.varstack_raw[n] OP a;
    
    IMGLC(EQ, ==) IMGLC(NE, !=) IMGLC(LE, <=) IMGLC(GE, >=) IMGLC(LT, <) IMGLC(GT, >)
    IMCBS(Add, +) IMCBS(Sub, -) IMCBS(Mul, *) IMCBS(Div, /) IMCBS(Mod, %)
    IMCBS(And, &) IMCBS(Or,  |) IMCBS(Xor, ^) IMCBS(BoolAnd, &&) IMCBS(BoolOr, ||)
    IMCBS(Shl, <<) IMCBS(Shr, >>)
    IMCBS(CmpEQ, ==) IMCBS(CmpNE, !=) IMCBS(CmpLE, <=) IMCBS(CmpGE, >=) IMCBS(CmpLT, <) IMCBS(CmpGT, >)
    IMCBII(AddIntInline, +) IMCBII(SubIntInline, -) IMCBII(MulIntInline, *) IMCBII(DivIntInline, /) IMCBII(ModIntInline, %)
    IMCBDI(AddDubInline, +) IMCBDI(SubDubInline, -) IMCBDI(MulDubInline, *) IMCBDI(DivDubInline, /) IMCBDI(ModDubInline, %)
    IMCBA(AddAssign, +) IMCBA(SubAssign, -) IMCBA(MulAssign, *) IMCBA(DivAssign, /) IMCBA(ModAssign, %)
    IMCBAL(AddAsLocal, +) IMCBAL(SubAsLocal, -) IMCBAL(MulAsLocal, *) IMCBAL(DivAsLocal, /) IMCBAL(ModAsLocal, %)
    
    // INTERPRETER_MIDCASE_UNARY
    #define IMCU(NAME, OP) INTERPRETER_MIDCASE(NAME) valback() = OP valback();
    IMCU(Neg, -) IMCU(BoolNot, !) IMCU(BitNot, ~)
    
    INTERPRETER_MIDCASE(Goto) i = valpop().as_label().loc;
    INTERPRETER_MIDCASE(GotoLabel) i = n;
    
    INTERPRETER_MIDCASE(IntegerInline) valpush((int64_t)(iwordsigned_t)n);
    INTERPRETER_MIDCASE(IntegerInlineBigDec) valpush(((int64_t)(iwordsigned_t)n)*10000);
    INTERPRETER_MIDCASE(IntegerInlineBigBin) valpush(((int64_t)(iwordsigned_t)n)<<15);
    INTERPRETER_MIDCASE(Integer) valpush((int64_t)s.programdata.get_token_int(n));
    
    INTERPRETER_MIDCASE(DoubleInline)
        uint64_t dec = ((uint64_t)n) << iword_bits_from_i64;
        double d;
        memcpy(&d, &dec, sizeof(dec));
        valpush(d);
        
    INTERPRETER_MIDCASE(Double) valpush(s.programdata.get_token_double(n));
    INTERPRETER_MIDCASE(LocalVar) valpush(s.varstack_raw[n]);
    INTERPRETER_MIDCASE(GlobalVar) valpush(s.globals_raw[n]);
    
    INTERPRETER_MIDCASE(ArrayBuild)
        auto back = std::move(s.evalstack);
        s.evalstack = vec_pop_back(s.evalstacks);
        valpush(make_array(make_array_data(std::move(back))));
    
    INTERPRETER_MIDCASE(ArrayEmptyLit) valpush(make_array(make_array_data()));
    
    INTERPRETER_MIDCASE(ArrayIndex) valreq(2);
        auto n = valpop().as_into_int();
        auto & val = valback();
        auto a = val.as_array_ptr_thru_ref();
        if (val.is_array()) val = (*a->items()).at(n);
        else val = make_ref(a->items(), (size_t)n);
    
    INTERPRETER_MIDCASE(Clone) valpush(valpop().clone(false));
    INTERPRETER_MIDCASE(CloneDeep) valpush(valpop().clone(true));
    
    INTERPRETER_MIDCASE(ArrayLen)
        auto & a = valback();
        a = ((int64_t)a.as_array_ptr_thru_ref()->items()->size());
    
    INTERPRETER_MIDCASE(ArrayLenMinusOne)
        auto & a = valback();
        a = ((int64_t)a.as_array_ptr_thru_ref()->items()->size() - 1);
    
    INTERPRETER_MIDCASE(ArrayPushIn) valreq(3);
        auto inval = valpop();
        uint64_t n = valpop().as_into_int();
        auto v = valpop();
        Array * a = v.as_array_ptr_thru_ref();
        if (a->items()->size() < n) THROWSTR("tried to access past end of array");
        insert_at((*a->items()), n, std::move(inval));
    
    INTERPRETER_MIDCASE(ArrayPopOut) valreq(2);
        uint64_t n = valpop().as_into_int();
        auto & v = valback();
        Array * a = v.as_array_ptr_thru_ref();
        auto ret = (*a->items()).at(n);
        if (a->items()->size() <= n) THROWSTR("tried to access past end of array");
        erase_at((*a->items()), n);
        v = std::move(ret);
    
    INTERPRETER_MIDCASE(ArrayPushBack) valreq(2);
        auto inval = valpop();
        auto v = valpop();
        Array * a = v.as_array_ptr_thru_ref();
        a->items()->push_back(inval);
    
    INTERPRETER_MIDCASE(ArrayPopBack)
        auto & v = valback();
        Array * a = v.as_array_ptr_thru_ref();
        v = vec_pop_back(*a->items());
    
    INTERPRETER_MIDCASE(ArrayConcat) valreq(2);
        auto vr = valpop();
        Array * ar = vr.as_array_ptr_thru_ref();
        auto & vl = valback();
        Array * al = vl.as_array_ptr_thru_ref();
        for (auto & item : *ar->items())
            al->items()->push_back(item);
        delete ar->info;
    
    INTERPRETER_MIDCASE(ArrayDelete)
        auto v = valpop();
        Array * a = v.as_array_ptr_thru_ref();
        delete a->info;
    
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
        auto & b = s.evalstacks.back();
        valreq(count);
        for (size_t n = 0; n < count; n++) b.push_back(vec_pop_back(s.evalstack));
    
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
