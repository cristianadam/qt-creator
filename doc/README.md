# Documentation Projects in This Repository

The qtcreator repository contains the sources for building the following
documents:

- Qt Creator Manual
- Extending Qt Creator Manual
- Qt Design Studio Manual

The sources for each project are stored in the following subfolders of
the doc folder:

- qtcreator
- qtcreatordev
- qtdesignstudio

For more information, see:
[Writing Documentation](https://doc.qt.io/qtcreator-extending/qtcreator-documentation.html)

The Qt Design Studio Manual is based on the Qt Creator Manual, with
additional topics. For more information, see the `README` file in the
qtdesignstudio subfolder.

The Extending Qt Creator Manual has its own sources. In addition, it
pulls in API reference documentation from the Qt Creator source files.

Note: Please build the docs before submitting code changes to make sure that
you do not introduce new QDoc warnings.
