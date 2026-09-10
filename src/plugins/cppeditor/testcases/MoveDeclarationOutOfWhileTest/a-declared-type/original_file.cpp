struct Foo { int bar; };
Foo *g();
void f()
{
    while (Foo *@foo = g())
        foo->bar = 1;
}
