namespace N {
void f();
class C {
    friend void f();
};

void N::f() {}

}
