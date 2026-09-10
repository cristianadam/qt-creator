struct Foo { int bar; };
Foo *g();
void f()
{
    if (Foo *@foo = g())
        foo->bar = 1;
}
