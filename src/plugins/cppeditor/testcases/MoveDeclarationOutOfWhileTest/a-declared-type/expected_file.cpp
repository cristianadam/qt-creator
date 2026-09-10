struct Foo { int bar; };
Foo *g();
void f()
{
    Foo *foo;
    while ((foo = g()) != 0)
        foo->bar = 1;
}
