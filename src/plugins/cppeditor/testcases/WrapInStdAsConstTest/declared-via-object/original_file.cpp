template<typename T> class QList
{
public:
    T *begin();
    T *end();
};

struct S { QList<int> list; };

void f()
{
    S s;
    for (int x : @s.list) {}
}
