
// built-in function definitions. customize however you want!

void f_print_inner(DynamicType * val)
{
    if (val->is_int())         printf("%zd", val->as_int());
    else if (val->is_double()) printf("%.17g", val->as_double());
    else if (val->is_func())   printf("<function>");
    else if (val->is_label())  printf("<label>");
    else if (val->is_array())
    {
        printf("[");
        auto & list = *val->as_array().items();
        for (size_t i = 0; i < list.size(); i++)
        {
            if (i != 0) printf(", ");
            f_print_inner(&list[i]);
        }
        printf("]");
    }
    else if (val->is_ref())
    {
        printf("&");
        f_print_inner(val->as_ref().ref()+0);
    }
}
void f_print(PODVec<DynamicType> & stack)
{
    DynamicType v = vec_pop_back(stack);
    f_print_inner(&v);
    printf("\n");
}
void f_printstr(PODVec<DynamicType> & stack)
{
    DynamicType v = vec_pop_back(stack);
    
    Array * a;
    if (v.is_array())
        a = &v.as_array();
    else if (v.is_ref() && v.as_ref().ref()->is_array())
        a = &v.as_ref().ref()->as_array();
    else
        return;
    auto & list = *a->items();
    for (size_t i = 0; i < list.size(); i++)
    {
        if (list[i].is_int())
            printf("%c", (char)list[i].as_int());
    }
    puts("");
}
void f_first(PODVec<DynamicType> & stack)
{
    DynamicType v = vec_pop_back(stack);
    Array * a = v.as_array_ptr_thru_ref();
    stack.push_back((*a->items())[0]);
}
void f_last(PODVec<DynamicType> & stack)
{
    DynamicType v = vec_pop_back(stack);
    Array * a = v.as_array_ptr_thru_ref();
    stack.push_back((*a->items()).back());
}
void f_dump(PODVec<DynamicType> & stack)
{
    DynamicType v = vec_pop_back(stack);
    Array * a = v.as_array_ptr_thru_ref();
    
    for (auto x : *a->items())
        stack.push_back(x);
}
void f_sqrt(PODVec<DynamicType> & stack)
{
    DynamicType val = vec_pop_back(stack);
    if (val.is_int())         stack.push_back(sqrt(val.as_int()));
    else if (val.is_double()) stack.push_back(sqrt(val.as_double()));
    else THROWSTR("in sqrt: not a number");
}

//typedef void(*builtin_func)(PODVec<DynamicType> &);
//const static builtin_func builtins[] = {
static void(* const builtins [])(PODVec<DynamicType> &) = {
    f_print,
    f_printstr,
    f_first,
    f_last,
    f_dump,
    f_sqrt,
};
static inline int builtins_lookup(const ShortString & s)
{
    if (s == ShortString("print"))
        return 0;
    else if (s == ShortString("printstr"))
        return 1;
    else if (s == ShortString("first"))
        return 2;
    else if (s == ShortString("last"))
        return 3;
    else if (s == ShortString("dump"))
        return 4;
    else if (s == ShortString("sqrt"))
        return 5;
    else
        THROWSTR(ShortString("Unknown built-in function: ") + s);
};
