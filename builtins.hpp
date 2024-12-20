
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
        f_print_inner(val->as_ref().ref());
    }
}
void f_print(vector<DynamicType> & stack)
{
    DynamicType v = vec_pop_back(stack);
    f_print_inner(&v);
    printf("\n");
}
void f_printstr(vector<DynamicType> & stack)
{
    DynamicType v = vec_pop_back(stack);
    
    Array * a;
    if (v.is_array())
        a = &v.as_array();
    else if (v.is_ref() && v.as_ref().ref()->is_array())
        a = &v.as_ref().ref()->as_array();
    else
        return;
    for (auto n : *a->items())
    {
        if (n.is_int())
            printf("%c", (char)n.as_int());
    }
    puts("");
}
void f_first(vector<DynamicType> & stack)
{
    DynamicType v = vec_pop_back(stack);
    Array * a = v.as_array_ptr_thru_ref();
    stack.push_back((*a->items())[0]);
}
void f_last(vector<DynamicType> & stack)
{
    DynamicType v = vec_pop_back(stack);
    Array * a = v.as_array_ptr_thru_ref();
    stack.push_back((*a->items()).back());
}
void f_dump(vector<DynamicType> & stack)
{
    DynamicType v = vec_pop_back(stack);
    Array * a = v.as_array_ptr_thru_ref();
    
    for (auto x : *a->items())
        stack.push_back(x);
}

//typedef void(*builtin_func)(vector<DynamicType> &);
//const static builtin_func builtins[] = {
static void(* const builtins [])(vector<DynamicType> &) = {
    f_print,
    f_printstr,
    f_first,
    f_last,
    f_dump,
};
static inline int builtins_lookup(const string & s)
{
    if (s == std::string("print"))
        return 0;
    else if (s == std::string("printstr"))
        return 1;
    else if (s == std::string("first"))
        return 2;
    else if (s == std::string("last"))
        return 3;
    else if (s == std::string("dump"))
        return 4;
    else
        THROWSTR("Unknown built-in function: " + s);
};
