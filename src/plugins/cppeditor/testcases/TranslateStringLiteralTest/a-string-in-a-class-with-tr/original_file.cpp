struct C {
    static const char *tr(const char *s);
    void f();
};

void C::f()
{
    const char *s = "ab@c";
}
