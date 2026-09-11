namespace NS {
class C {
    void f();
};
}
void NS::C::f()
{
    @{start}C c2;@{end}
    g(c2);
}
