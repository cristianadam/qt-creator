template<typename T> class QList
{
public:
    T *begin();
    T *end();
};

void f()
{
    QList<int> list;
    for (int x : std::as_const(list)) {}
}
