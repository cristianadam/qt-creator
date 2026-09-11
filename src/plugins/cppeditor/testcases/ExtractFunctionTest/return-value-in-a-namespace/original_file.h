namespace NS {
class C {
    void f();
};
}
void NS::C::f()
{
    @{start}C c2;@{end}
    C c3 = c2;
}
