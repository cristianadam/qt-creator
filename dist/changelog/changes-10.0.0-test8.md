Qt Creator 10.0.0-test8
=======================

Below you have an excerpt of the output of:

    git log --cherry-pick --pretty=oneline origin/v9.0.1..HEAD

```
0b51a3a7838d3f9582ab6455ac29545179aafd2c GitHub Actions: Some cleanup
adfb5062f473cbc501ad7e0477498b954ff43329 Git: Do *not* use ctrlc stub for rev-list
7faad7b4e8a10281360ad1e07be8f024ee9d93df Android: Move a global regexp variable into a function
c2cfe596b955616081465a015485f94b18f021b9 Git: Reduce PATH searches for executable
728e73ea9db9383fb844cf07087844a624ee46e4 C++: Fix return type in preprocessor comparator
1e6933f48d9a2141755b9ed5549bd2f790d8d9e4 Doc: Reorganize the Debugger topic
70506ff1360cccccc81ccfd1cc6ba48152e4a8b4 Update qbs submodule to HEAD of 2.0 branch
9f2f2f3390fe88b120073be9a08da66cbdc95552 Translations: Change translation context prefix from "::" to "QtC::"
226799858c8a0da51f7f0b99b83308c60f30a288 Translations: Replace QCoreApplication::translate() with Tr::tr()
14280acfd956264d15a92c2976d65e5813836d32 Translations: Merge orphaned contexts
64aaf66c3b7ca5df95e6f6e2c85a42f905da9017 Proliferate Tr::tr in various places
6138414813c58162d948de203db4c70075d000b4 Tests: Remove a couple of tr() calls
ba34f00e1e67721cda2af8a2a2a436940b1e1e4a ModelEditor: Tr::tr()
3c6b8b08df9c5571f5eaed358262a818d66f3604 ModelingLib: Tr::tr()
fe91151f7ceed2ac2ef1264e757433e6b6b13076 Valgrind: Tr::tr()
91c00ec34f6562479ba150838a640075dc98e277 SilverSearcher: Tr::tr()
ab4516c85cf181db1cff033c4c619250dfd3fde9 Pdb: Make another builtins usage implicit
2b46d1943cde354b45e31ff0c04775661cd82409 QmlPreview: Tr::tr()
4f55dbdd386af0569420aef4c9b6b4277632bcbd CompilationDatabaseProjectManager: Tr::tr()
c72638ed74478071f89806f99e0f7aafa5859f50 QtcProcess: Introduce a way to track long-running blocking processes
a8bc009595521a2a0cee729ca55fe528c0845266 Debugger: Avoid potential crash
f6afedeea603e4f561eb96585aaae2a456703f53 Debugger: Do not crash when using a builtin as value
4b9aaf6ca11a73d083484cbfa33fcd8a7b25d274 ExtensionSystem: Remove the IPlugin back pointer to pluginspec
2a2f6afb040d1bc6ac2197b20414839d1a5ff582 Merge "Merge remote-tracking branch 'origin/9.0' into 10.0" into 10.0
b29cb4efe1116492deda8babf028d8c71cd01730 Merge remote-tracking branch 'origin/9.0' into 10.0
3023b6ab0396661d3b4185d19fba96ed3cbb671a Translations: Remove stray Q_DECLARE_TR_FUNCTIONS(...)
3d57b1868b512c647407ee278bb4162495da8708 Fossil: Force open a repository by default
8491441257a9ca8007bb0ab7899f15887fe1c024 TextEditor: Speed up updateCurrentLineHighlight
ff65caf62faa1e5c9411c26ccacbf21263fa3393 SquishTests: Explicitly convert QString to str
f06bc4ab5ad5669b26ce4cbe42a4c75a15747c52 Docker: Fix searching for clangd
5782c7e6f3db4c7194626a7ef62f56cbc688dfdb PerfProfiler: Use new plugin test object setup
1d5c58775854efdbf6f53277326cd2595260e5fd README: Add some more information wrt running and installing
bad6b2c290d344ec467952e7101142bee2bc3c64 SquishTests: Avoid crash in test
4e34f1781e0ad4e90475c5dce78a41af79600b64 Clangd: Convert paths in diagnostic messages
1523f49e5cc87c04f3528947ec06d0b740704132 Utils: Add PathFilter to FilePath::searchInPath
f6d1a4aaf4661b1d1515e31a747b12d73b852761 ClearCase: Fix bug in ClearCaseSync runProcess
61eb25af6d0e2c08715dc978e68dfcc598162584 auto-setup: cleanup script
da7383387d7e2c3c358a5de61388c6a87a0f2414 Clangtools: More FilePath
9ad60cd8911f58641f07d62187f03daddd85fecf ClangTools: Use FilePath in AnalyzeUnit
15841807d5fab4cbdc6810da9f4590aa160ee2a5 SquishTests: Update createNewQtQuickApplication()
4b9a60614cd2c2d314a1ccf199349f703a584b6d Utils: explicit constructor for QtcProcess
98b1fcb7b312f33014baff0a5b2e0e8da5445774 Translations: Use QCoreApplication::translate() instead of tr()
86a14c8b272564e222853bf848572aea683c2b13 ClangGlobalSymbolFilter: Avoid downcasting to WorkspaceLocatorFilter
86750c5772eef25720ccd1e26d2a8fe809dc159c Utils: Don't check fileAccess before adding Devices
78ae704d77b1879b2cb5bb1b37f5c804f7e2f9fc Utils: Enable internal documentation
b59e632ac37dd212f925491c24243a5f4997b38a ExtensionSystem: Another way to have plugin tests
a01261d7984898e860ce8ed8861871345be19685 ClearCasePlugin: Fix runCleartoolProc
40b295cbab89ac22c09bff391d4ddf89e7a42cea Utils: Move some translated strings from .h to .cpp
a6ccfb09e588f3500a34b6f240350d77d3403a29 CMakePM: Copy auto package-manager to ${buildDir}/.qtc/ directory
f5bd33027756e77890677e32a31f6796ca44481d Qbs: Update some qbs files
7cb309a2db20fe91e766fdd81cbae6d79eb92d81 Coco: Tr::tr()
3f8240a7de9be6df01b5f56ce57d2750bcbbf187 TextEditor: Some fileName -> filePath renaming
a7bf65c2888f179a8697912be521ebe1b33d0efd Bookmarks: Use more FilePath
a86299d573afe79c25054594531cf314b2235b90 Squish: Tr::tr()
dbf017c11e2ecdb2cbd41f7e306230b71c1055a4 Marketplace: Tr::tr()
74bf8fd5f7c8cf972c7d2e756268d5aa8b7f9c8d GitLab: Tr::tr()
bd2ca236e11b25ae28e8f519258627f66a34ddfd CPlusPlus: Check maximum include depth in lexer
06b579a75be7924462a3cdfe9ecb8e60b3e01524 McuSupport: Fix handling of FilePath
ac252c2d49c6afb2a0f9faac4ffbf2f58e736ce9 Doc: Make the locator more visible in the other parts of the docs
85bed557f3fd613d5e79832323bb2966954a798f Utils: Use more FilePath in TerminalCommand
49561eaeb82e1a207a1f5a03c8a222ed35fb9329 qtquickapplication wizard: Adapt to CMake API change
075f39e54303d279f44ca887b51b607d13145849 CMake: Fix editing of CMAKE_PREFIX_PATH
997655e669ff105708d28ac4a201b3f824494860 ClangFormat: Fix build
239f79fbecf05c193a7223c956e234589fad194a CMake: Use FilePath functionality to extract qml module files
389b9503d74acfc5275f1b97a78645b1ec5f4ab1 ClangFormat: Tr::tr()
48affa1889ab7285ac84a50bd2df34f50c2b1bfb Translations: Remove header includes of *tr.h in .h files
ccbee9bf42a934a81ac06181e09a77b530ee765e qmldir parser: Handle internal types with versions
37488c69b86750b3bdc5f997a7866935a07837df Utils: Remove 4.8 compatibility code for reading terminal settings
0bd52ea2754bf8fc39027913629e18f42fdd4ef3 ADS: Fix SPDX-License-Identifier in advanceddockingsystemtr.h
4d8a18c07da4383393350d12353c87de3f612da3 ProjectExplorer: Fix build
c651a1e290b48ea002df3de738d445f9984c654f ClangTools: remove arrow from analyze current file button
9db70d8810dbee8c3b54e4d69f9dfcee27f90259 Translations: Fix stray QApplication::translate() calls
2356f28647c04718919a090a4784c9453cd0c02a Translations: Add missing "::" prefixes to QT_TRANSLATE_NOOP
8a8b50e8b5d8788e917c3d24db10ef29c4a5462e ProjectExplorer: Proliferate Utils::FilePath use
5728f09facdaa4c2e358221f3b9ca66f8651c049 Editors: Fix message box when opening broken code style file
432d918329d873e4b89b07670312e63d4121ba99 TextEditor: Use FilePath in Command
012a2a6cd828188837a1d9d2b060e8718c7e2775 Fix build on Windows / MSVC 2019
f94b41313b80701df0ce37e0a4e6bb0c01ec28d2 Utils: Fix MSVC size_t -> int warning
3a87b458319db692432e4f4d4197509e71e9d5db Fossil: Tr::Tr()
521a23df6e63fd0ba394f728ddc0a4bb271e926f Fossil: Inline revertdialog.ui
```
