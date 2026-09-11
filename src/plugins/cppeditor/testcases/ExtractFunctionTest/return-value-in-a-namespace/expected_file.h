namespace NS {
class C {
    void f();

public:
    C extracted();
};
}
inline NS::C NS::C::extracted()
{
    C c2;

    return c2;
}

void NS::C::f()
{
    C c2 = extracted();
    g(c2);
}
