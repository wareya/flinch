
// built-in function definitions. customize however you want!

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
