namespace N {
struct C {
    void f();
};
}

void N::C::f()
{
    const char *s = QCoreApplication::translate("N::C", "abc");
}
