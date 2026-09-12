struct QObject { void connect(); };
struct XmarksTheSpot : public QObject {
    Q_PROPERTY(int i@t READ getIt WRITE setIt NOTIFY itChanged)
};
