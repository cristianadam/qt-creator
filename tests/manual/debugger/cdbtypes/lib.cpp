struct LibProbe { int probeValue; };
struct SecondProbe { int secondValue; };

extern "C" __declspec(dllexport) LibProbe *inferiorLibProbe()
{
    static LibProbe probe = {4711};
    return &probe;
}

extern "C" __declspec(dllexport) SecondProbe *inferiorSecondProbe()
{
    static SecondProbe probe = {8822};
    return &probe;
}
