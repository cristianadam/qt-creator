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
    for (int x : std::as_const(s.list)) {}
}
