template<typename T> class QList
{
public:
    T *begin();
    T *end();
    const T *begin() const;
    const T *end() const;
};

namespace std {
template<typename T> const T &as_const(T &t);
}

void f()
{
    QList<int> list;
    for (int x : std::as_const(@list)) {}
}
