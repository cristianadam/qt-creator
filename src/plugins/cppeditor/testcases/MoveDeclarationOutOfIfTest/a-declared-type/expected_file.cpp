struct Foo { int bar; };
Foo *g();
void f()
{
    Foo *foo = g();
    if (foo)
        foo->bar = 1;
}
