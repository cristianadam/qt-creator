inline int extracted()
{
    int i = 1;

    return i;
}

void f()
{
    int j = 0;
    int i = extracted();
    j = i;
}
