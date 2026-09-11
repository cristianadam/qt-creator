namespace N {
struct C {
    void f();
};
}

void N::C::f()
{
    const char *s = "ab@c";
}
