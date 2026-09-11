struct C {
    void f();
};

void C::f()
{
    const char *s = QCoreApplication::translate("C", "abc");
}
