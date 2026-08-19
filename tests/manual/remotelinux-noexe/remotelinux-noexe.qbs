// Reproducer for the empty "Executable on device" case. Open this project with
// a cross-device kit (local build device, Remote Linux or QNX run device). The
// application is deliberately not installed, so nothing maps the built
// executable to a device path: the run configuration's "Executable on device"
// resolves empty and RunConfiguration::createNoRemoteExecutableIssue() reports
// it. See noexe-testplan.md.
Product {
    type: "application"
    name: "remotelinux-noexe"

    Depends { name: "cpp" }

    files: ["main.cpp"]

    Group {
        name: "instructions"
        files: ["noexe-testplan.md"]
    }
}
