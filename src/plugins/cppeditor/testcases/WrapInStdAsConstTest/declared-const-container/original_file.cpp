template<typename T> class QList
{
public:
    T *begin() const;
    T *end() const;
};

void f()
{
    const QList<int> list;
    for (int x : @list) {}
}
