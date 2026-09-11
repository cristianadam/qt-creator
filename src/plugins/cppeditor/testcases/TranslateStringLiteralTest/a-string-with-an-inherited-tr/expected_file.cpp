struct B {
    static const char *tr(const char *s);
};

struct C : B {
    void f();
};

void C::f()
{
    const char *s = tr("abc");
}
