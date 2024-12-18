
// built-in function definitions. customize however you want!

void f_print_inner(DynamicType * val, const char * eol = "\n")
{
    if (val->is_ref())
        return f_print_inner(val->as_ref().ref, eol);
    
    if (val->is_int())         printf("%zd%s", val->as_int(), eol);
    else if (val->is_double()) printf("%.17g%s", val->as_double(), eol);
    else if (val->is_func())   puts("<function>");
    else if (val->is_label())  puts("<label>");
    else if (val->is_label())  puts("<label>");
    else if (val->is_array())
    {
        printf("[");
        auto & list = *val->as_array().items;
        for (size_t i = 0; i < list.size(); i++)
        {
            if (i != 0) printf(", ");
            f_print_inner(&list[i], "");
        }
        printf("]%s", eol);
    }
    
}
void f_print(vector<DynamicType> & stack)
{
    DynamicType v = vec_pop_back(stack);
    f_print_inner(&v);
}
void f_printstr(vector<DynamicType> & stack)
{
    DynamicType v = vec_pop_back(stack);
    
    Array * a;
    if (v.is_array())
        a = &v.as_array();
    else if (v.is_ref() && v.as_ref().ref->is_array())
        a = &v.as_ref().ref->as_array();
    else
        return;
    for (auto n : *a->items)
    {
        if (n.is_int())
            printf("%c", (char)n.as_int());
    }
    puts("");
}

typedef void(*builtin_func)(vector<DynamicType> &);

builtin_func builtins[] = {
    f_print,
    f_printstr,
};
unordered_map<string, size_t> builtins_lookup = {
    { "print", 0 },
    { "printstr", 1 }
};
