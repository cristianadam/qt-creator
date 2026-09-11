inline int extracted()
{
    int i = 1;

    return i;
}

void f()
{
    int i = extracted();
    g(i);
}
