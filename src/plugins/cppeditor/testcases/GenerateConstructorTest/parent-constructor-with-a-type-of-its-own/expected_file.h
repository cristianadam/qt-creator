namespace N{
struct Value{};
struct Bar{
    Bar(const Value &v);
};
}
class Foo : public N::Bar{
    int test;
public:
    Foo(int test, const Value &v);
};
