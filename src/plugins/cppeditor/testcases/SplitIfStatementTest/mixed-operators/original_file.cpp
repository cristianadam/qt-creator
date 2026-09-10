void f(bool a, bool b, bool c, int x)
{
    if (a || b @&& c)
        x = 1;
}
