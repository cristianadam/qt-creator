template<typename T> class QList
{
public:
    T *begin();
    T *end();
};

QList<int> &theList();

void f()
{
    for (int x : @theList()) {}
}
