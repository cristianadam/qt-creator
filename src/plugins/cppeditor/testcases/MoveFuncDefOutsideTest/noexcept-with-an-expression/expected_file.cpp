class C {
    void f() noexcept(false);
};

void C::f() noexcept(false) {}
