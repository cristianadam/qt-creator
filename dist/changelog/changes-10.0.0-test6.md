Qt Creator 10.0.0-test6
=======================

Below you have the output of:

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
d14c561cf6e7af762832024bcdb0a258a29fc6a5 Fossil: Inline pullorpushdialog.ui
89880907f2dae5ef063027e3b74f4e67d3bbd39a Debugger: Simplify paramenter setup on Windows
cbf2b23787fde9065a082de3a101bc5d4bbd6fd6 QmlDesigner: Remove unused global static
d2656c60e6525e55fabf2e2755768172a8a29433 AutoTest: Widen scope for run under cursor
9277bb5111405628adaf79f9a76b7bd17203ee57 AutoTest: Extract function to get active frameworks
96185ca4f1ed163cdded05a9b0adf07f3f7e7c22 Utils: Use QtcProcess in FilePath::fileContents
8096b9f52476b119e61c8b5809535ad865b5e362 Update qbs submodule to HEAD of 2.0 branch
78cf3eb671abd802d7d0f693b7d0cf956de4de06 Docker: Check writeData
84ba73be8ca914c081c478bbd91f5b43f001229e RunExtenstions: Remove StackSizeInBytes
fbcf0fb3bfe0a4776ed1771983d3944d6fa78e16 ClangCodeModel: Fix initial processor state
96ebe93ecb529cc8d31cb743f6c3970fc55ae07b Debugger: Rebuild AttachCoreDialog
1e92039dc1274947929137d83e33ca1e1dac0af9 Utils: Fix build of sdktool
e780ca991c8c40459bf5ba2b46006b48ab865093 Docker: Use rootPath() for Shell
9d6b3b4cb4607ee09e389a9ab295f1a4b8de4670 Utils: Remove status= when using dd
5342fa5757271501f8cd0b270ccb2eeebf7814df Fossil: Register wizards path only once
a459a70ed31062df74975a3a67f0788b8a13e7ee Haskell: Convert to SPDX-style copyright headers
541aafecbb63c574c6d118833d52ed3628c990fd QML: Fix crash when opening context menu
8512aba9ebfa4979d5d850894a056b84ad3edc71 Doc: Add a short overview of what debuggers do
229348853689b936b4bbdb907762abb619b6801f ClangCodeModel: prevent accessing nullptr
516fce6f532cb9c6492c998fb211b55ab34cad73 Utils: Add FilePath::tmpDir and createTempFile
02777c4179ec46105ee5557621a8903c4ed4820f Fossil: Inline configuredialog.ui
0c1b59fb1aa1dca243ef4f65db286a935f23fc17 Utils: Use isSameDevice in copyFile
144dff8d744c5456cac4386dfe10bd87690d0ad2 Gdb: Use path() of executable
3358f94cb76a0a566a79085927dfc121eb42f233 CMake: Detect cmake from ARM homebrew on macOS
c1656c2f3c2193a902d31048c3a115254bb99528 UpdateInfo: Fix never ending progress spinner in settings
97323e14f0fc69621bfefa567646e20424969518 GitHub/Linux: Fix missing chrpath for deployment
78cad1813563d1276ef64d224b6b1bb16504c3e1 GitHub: Create and upload Debian packages
c6853ff32a123a3fe4956d7667cf53b0138c19c1 GitHub: Fix ccache archive downloading if there is none
447f8b80c3508d1ff5dece65fdd1f56a5d2930ea Doc: Remove double words and fix a typo
f1c302af0c950ec3d1cf05f552bef8aee51760da Fossil: Inline fossilcommitpanel.ui
c4f6887b1d880e17899fdf0d1643c5d60caa951f GlslEditor: Tr::tr()
b919171375652d612ad5bc1a31fd9f9a3402a10a EmacsKeys: Tr::tr()
c392ccc0198d1c9339736441b7e099d2f45a2775 Translations: Remove empty "<location/>" tags
077401b9ce216050c7dba37b215cebbc8fdc5044 Ios: Merge orphaned .ts context into "::Ios"
e1538b7e321ec39c19568d014e5b547dfa709621 VcsBase: Merge orphaned .ts context into "::VcsBase"
b0e289e09627f7bdfd2bd5de273b533a5b5ed2d0 Valgrind: Merge orphaned .ts contexts into "::Valgrind"
2376a5d125c26ce7599f43ba18c4b524328e0f4f Tracing: Merge orphaned .ts context into "::Tracing"
8ad7066e0d83eb94bdd2b7937bf9e5add5295a3b TextEditor: Merge orphaned .ts context into "::TextEditor"
93d38544013b7898679250eea0127853cf27910b ResourceEditor: Merge orphaned .ts contexts into "::ResourceEditor"
38336a5869f2246d6dca8efe8fdfb2182d4bf0e0 RemoteLinux: Merge orphaned .ts context into "::RemoteLinux"
93f0e589803c3be3078e65328f55f6324c9eefb2 QtSupport: Merge orphaned .ts contexts into "::QtSupport"
6a993b50cfcfc26e8da43c7106ac1eacc89b8d5a QmlPM: Merge orphaned .ts context into "::QmlProjectManager"
73f4dd768cf60e50d264220552fd6979c00a0ef3 QmlProfiler: Merge orphaned .ts context is "::QmlProfiler"
d570e6772534fc161429d74c26002da9dba12be5 QmlJSEditor: Merge orphaned .ts contexts into "::QmlJSEditor"
ef07a9d722703ad8ba623e3241e7df7f04a372de QmlJS: Merge orphaned .ts contexts into "::QmlJS"
d46d196a8c7b2507f33f9a5462537123869a7766 QmlEditorWidgets: Merge orphaned .ts contexts into "::QmlEditorWidgets"
00e4efefa246ab1f0061d00e171c7ea37f9beb1f ProjectExplorer: Merge orphaned .ts contexts into "::ProjectExplorer"
21862c79ed00e28c2bea4e9e3270da9b9464576e LSProtocol: Merge orphaned .ts contexts into "::LanguageServerProtocol"
c0234c0be5472435515d083f710d8ea5d7383caf ImageViewer: Merge orphaned .ts context into "::ImageViewer"
5dfa6c003adedc4b397a328e32460ab3d0a775ab Help: Merge orphaned .ts contexts into "::Help"
5246c59686758f229b824e02cc7538da18df71ea Debugger: Merge orphaned .ts contexts into "::Debugger"
7cfbe9475e94d995592d3e0fad92eaf8674f799a CVS: Merge orgphaned .ts context into "::CVS"
98706a558fcdd31b72e9394efd706b62d9634038 CppEditor: Merge orphaned .ts contexts into "::CppEditor"
110c4135a66edf7cbc8390269794fcefc602bf16 Core: Merge orphaned .ts contexts into "::Core"
135eab2237590065a137444d6f87d63848349c9d CodePaster: Merge orphaned .ts contexts into "::CodePaster"
c10a93771998c2888d443b238ce7aaaa6de860f1 ClangCodeModel: Merge orphaned .ts contexts into "::ClangCodeModel"
732dee43ff2c1cb1f6a8a6b52fc1184c029d26b5 CMakePM: Merge orphaned .ts contexts into "::CMakeProjectManager"
f7c268c23d8c658a9c9511e0f9dc6bdd679e4889 AutoTest: Merge orphaned .ts contexts into "::Autotest"
930312546d0d715c7378de882f770005e33840ea ClangFormatPlugin: Don't leak ClangFormatStyleFactory
10295fc0fae05b029b99ee5f8107eb74fc015209 PublicKeyDeploymentDialog: Reuse new StringUtils methods
738819ec71261009e650ae378ffcafe7512841ce Doc: Replace "provide" with something simpler
33badfa660f57fe62547f9c609d58088c2165280 PathChooser: Don't leak menu
a423caba13831c76b8651c7876bd54ecad0b0d94 ClangFormatBaseIndenter: Initialize llvmFileSystemAdapter
d2781354973154fffdf81e412fa9e60d2deb9a72 Merge remote-tracking branch 'origin/9.0' into 10.0
82d2f02c3fccbed0cbc10c151d3f736029a6d582 DiffEditor: Fix compile
37a86061d8f0d84c1584c927292fbd89e5347ab3 CMakePM: Adapt to change in FilePath
77616b808642821f32162c1c03398d202080316b DiffUtils: Get rid of PatchFormattingFlags
d829d9ff336629648971b47190a8d2a5840e5d03 StringUtils: Add trimFront(), trimBack() and trim() methods
ebcd8bcd51383ff334b77cf4d4028c4d1cceeb36 DiffEditor: Simplify DiffEditorDocument::plainText()
bf64af6f521c267c3389ca7d3fff222af1cc6106 DiffUtils: Simplify file name formatting
202b696677c13079dd503d0f814aecdc64bfa00b DiffEditor: Fix a crash when "No difference"
ce0f48e0f9b9e59c2c876da1eb923622730df6e4 Doc: Replace "contain" with "have" to simplify language
dd3f5d3a6a6803b0ac6ce37d861ab27e0b25eb19 SquishTests: Fix designer test
e771560ec75d55962d4fbc39f6051b1d71f07bcc ClangCodeModel: Do not traverse the AST in the semantic highlighter
801709f72ea78ade76f0710f930fa24dc50be0b3 ProjectExplorer: Show recent projects with native paths
0d0ce383be825dcb3cd904d4fd791d107ec75182 RemoteLinux: Reuse StringUtils::joinStrings()
5d96c3a4e9f2538b8269134e034f26557d348de7 ProjectExplorer: Reuse StringUtils::joinStrings()
383c2205d0cde9e766723619c354e8014755cd1c QtSupport: Fix output formatter test
2d8d27c9ba97f9dc22bce23f1c1d85a56fdd1310 StringUtils: Add joinStrings method
f48b8b7bcb42d1d79e4f570a2ec1ec9b2d43b2ff AutoTest: Avoid potential crash
d7308cc7a69c150383d39e4461fd4acfc4f9c425 ClangCodeModel: Ignore built-in types
020320ab784401b4a240ee242b2298568482db57 Android: Add extension to the package name in SdkManager
7c6a00b4e703b0d976f79598829531e8646f1ce0 ClangCodeModel: Ignore number literals
8655603c2e1e5394773160ac1b6756bc89382373 Utils: Improve readability on output windows
2eb9338be0a30572a07c92ac9a59613383fdc055 CMakePM: fix presets sort assert with MSVC in debug mode
d8b2673ad1504603dbd73752ee87a22f97d507fa McuSupport: Update qmlproject wizard to stable API
51cbfd77db86f3d35ed80d5fdb75bc8fdb11a773 CMakePM: Improve CMake presets kit config hashing
c31522731ce9790b84e5d01e512c0f723758ceb1 ClangCodeModel: Ignore boolean and pointer literals
f952918390ea59ae5ed277c1908bac6ba4b60b4e CppEditor: Add missing include and ifdefs
209e3d0e66972ea63c7496a731b1757216641691 CppEditor: Fully handle raw string literals in the syntax highlighter
6570895c0b388801285c5a5a72c21e7e4a0f07ea TaskTree: Some cosmetic polishing after last refactoring
1d6d01952b0d59b153f8bf82d7f6f358c52d551a SquishTests: Fix tst_cmake_speedcrunch
9c9e87d50903117d4fd188c524b5fe96654a9183 ProjectExplorer: Fix link generation for "note:" msvc compile output
7fbb41c10c6a0b1660ed4601e133d9ec23766956 RemoteLinux: Cache device environment
fb6fc2bde8c3202a43dee2d2b1b101a29229f0e5 Utils: Add combine with device env option
cf692bd2f6ac8b4a83c0ed69dece4c34161ab494 CppEditor: Add soft assert against impossible token location
dfd8c8c98df59bbbb6a187abd5dabbd0b5b72041 QmlEditorWidgets: Fix wrong include
476f67cce5797bd9639c3bc3e29c332af64557c0 SquishTests: Fix default settings test
092209fa848996840216ce6ad5cb8d7541cd6e40 CMake: Fix potential null deref
a0657f5d6074566603ab8b50517ec96e5aa0c61a Debugger: Initialize members in WatchItem
823b8c77a5278b6c99c40cf418291a4d4320d6ca QmlEditorWidgets: Tr::tr
200a66644ef3d02bfb9969f6e9010f35fbec62ae QmlEditorWidgets: Inline contextpanewidgetrectangle.ui
4d6827dde9248b6b47949b535ae361e8c6b04cfa Utils: Tr::tr
17b28909a99cce2dc908ad2ffbb43a85539fa526 QmlJS: Tr::Tr
fc8b81f2cb0b8acb7cdeb6109539869d42dd7802 QmlEditorWidgets: Inline easingcontextpane.ui
5445ff511ad788f93f2bc3261aed58e2f7761258 QmlEditorWidgets: inline contextpanewidget[border]image.ui
5733fe71f41d6265198c787ae42eb9fd554c6eaa QmlEditorWidgets: inline contextpanetext.ui
d8b5f32e5aff3601492106c1515ca3987e48950d QtSupport: Fix missing object name
45d72db1b4545c643f6d39ee392b8d4d3c9b3851 TaskTree: Add 2 more variants of NestedParallelError test
f8d4e485c244fcbea3433f9c09241a944f583ecb TaskTree: Add a test for running empty tree with group handlers
5dc4cd837f271d5f297d3f5efc3ca4a2bfee6bbe TaskTree: Divide internal data into ConstData and RuntimeData
b177f925d1ef36b4b29c05fab92c6a5d0ed7bc3d Remove unneeded includes of QFutureWatcher/Interface
54326a127eece7cb5c5387b0a0ac73cb734e808b SquishTests: Use openDocument()'s return for verifying project tree
6a888242d3b874d71624ef065fcf9a1fba425227 Squish: Close opened squish files
1e411701238f9353cd3cb54e2898b9cf707ff6bf Squish: Improve handling of license issues
2b00256ebf3b06bfcaf39f781ace42c0474a1257 Squish: Fix removing a test case
f7c35a4a32158efd2ec1753569969434e049035f Squish: Remove action for removing a test suite
be9a01252dfe2378e19ff984b557f254898468d0 ClangCodeModel: Make use of new semantic token in clangd
df3e7e5bdd2d51da396e20caf2f4908e5528a661 Merge "Merge remote-tracking branch 'origin/9.0' into 10.0" into 10.0
93201df992113e16b3827a8528aac230790eaf39 ClangFormat: Pimpl ClangFormatConfigWidget
6758b5f69fff6a69f4cc27f8ec81d75f4eccd099 Utils: Move QtcProcess::normalizeNewlines() into StringUtils
825fea1fe94f1821ba9093ddd571ac4abd8b972d TaskTree: Fix flakiness of DeeplyNestedParallelError test
1fbc707b3f47b7ed7acadb3b4af55a63d4c4bc01 Fossil: Convert to SPDX-style copyright headers
0dab651ccd98135212961aa653c256b729cfe692 Merge remote-tracking branch 'origin/9.0' into 10.0
739d4942e2ed35fe92f11310d80632aa288fa44c Merge remote-tracking branch 'origin/master' into 10.0
4bfdfc2fbc535f3ca7508a7a7c95356b8c528d08 ProjectExplorer: Fix potential division by zero
c5ea7560271334ab51379c96abbcebd542b6d312 Doc: Integrate Fossil docs to the manual
6c8afcde88d25ea889492d4bf0725e449fa7efc5 Core: Make access to remote terminal easier in code
128b21c95989dc70c3ee579e9625667ed3967ebe Fossil: Import from super repo
862f484af59cea06b99a743316c2ffcc0a252686 McuSupport: Extend CMake targets tree for QtMCUs QmlProject projects
3d79244dd5d1ab375faa61f4d96b180985ff06f4 SessionNameInputDialog: change default button to "... and Open"
53696ee71e8b0b06d9c71d14fa581abbb5d7cab4 SessionNameInputDialog: disable accept buttons when name is empty
e20bdfae4de90401a518135bc22958549dceda66 ClangFormat: Adapt to changes in LLVM 16
50ee70fcee8ca129665039062064eec67ff7c4e9 RemoteLinux: Remove AbstractRemoteLinuxDeployService::setDevice
97002f934fa0b97711aea91f54e1aa45af15d123 QmlProjectManager: Fix the "Qt Quick UI Prototype" wizard
ad5460e5f5e3806a19c79ffd0a46db4432093dab Squish: Fix cancel handling
7dc1e2031f7381500292d98f9acff6e9a33cfbf4 MarketPlace: Avoid needless copy
ec43a6c005cd719e80305f9f1666c1a7f1be7bd1 QmlProjectManager: FilePathify further
fa2ea6fd97c9e34b317f71550388c3f97aa18601 QtQuickApplication template: Support more modern API
86ffc29239ec891e307b2c5865ce56c53841e388 Git: Avoid passing strings to resolve text encodings
7cd4c295c71c7aca2db7ef5a76272f86a51fc2ae CppEditor: Initialize member in GetterSetterRefactoringHelper
92b23cb8274459a3977b2f66d45c814e997f6045 Nim: Use simple wrapper classes for project generation
8cb0858574d3daef69105af87234149ade7d4d4d Improve console app template
2f94e93334af366589fb2c5dcbff33a0694b6074 "New Class" wizard: Add QML_ELEMENT checkbox
2afe3a6ff2da5daeb59f6c89c9718527ecdc0375 Squish: Fix object map handling
3c51f499ac3d6bd39a0ef8e1e8e1e8be9bd649d3 Squish: Fix perspective handling
0a30b8aa26283b97431deebd0b2816bf5eb3e5af QmlProjectManager: Fix Qt Quick UI Prototype wizard
61fab0b73602f30a4ae7a2c09c7c1d1422160863 Git: Fix instant blame text encoding
04f521abcfbf68fc45ffcfc38557431914e744d8 CompilationDBPM: Adapt to FilePath change
803ecca075f3a627a97e3eef56d6ad60ca0a1849 ClangCodeModel: Fix soft assert
b05ec7dc3c604a9e04a912cbb73b98c3702b5f8f AutoTest: Avoid pass by value
06390f5b5342b32825e1cf2c90e10a6082d58867 CppEditor: Warn against renaming in generated files
4e8b7eee9ddf2feb8f7ced24bb9425539059e698 Cpp: support space ship operator in lexer
c780151aece1ca31dfe4a283058295469832d56a Add Qt Safe Renderer helper plugin
e3f6eca25d6899ece962c6e4c97860d5fd8f230c Utils: Move some code from QtcProcess to its only user
e95c1d3c148dcfa59764a6a49588cbb3fee9ca68 CPlusPlus: Fix tools build
37245204995979a613194d58eafcb03dae86cbec Merge remote-tracking branch 'origin/master' into 10.0
0f19fd6d0d6dab11e7a9e2aa32509420fdcc8ea5 TaskTree: Add tests for proper continuation unwinding
5bcb24cb7115c2efe08fb512dc9d5ae7c174a792 TaskTree: Fix continuation when synchronous stop appeared
6b11e1d5729167a62fb77b17251a65b126ff5c0f TaskTree: Revamp tests
55dd9330664dce88958b002dec63435e359a5776 Haskell: Import from superrepo
3a62e6d7f2bbaf1214a7df40584ae9a97fea5d38 ClangTools: reduce priority of clangtool processes
62e0b4052f34cbc6064e64893b18a266f3dc2f18 Remove unneeded includes of utils/runextensions.h
97976fd3cbdd57ed83cdc3e606196f2bb7ee1a78 ProjectExplorer: Refactor BuildSystem::extraCompilerForSource
49f3b8efd75a49c3a92e86a02088b4bcd40e0a61 ITestConfiguration: Introduce testExecutable() helper
040ef3b7d30541b2cfe8ece14eb8558e5ebd4b2e Docker: Use a optional<Environment> to handle the non-fetched state
dab859e7762cf3a9566f35b1bd3f2186a47d2bcb Utils: Replace Environment.isValid() with .hasChanges()
930cbdf68b7697435fcdc6726869a4f77066a0f7 RemoteLinux: Merge abstractremotelinuxdeploy{step,service} file pairs
a938a82ca85fbcc50bbd84ca683af077f2d60044 Boot2Qt: Use standard pattern for Qt version factory setup
c22b4b35e1e8205a3106dd1c9c04af6a3ea8ab7c SquishTests: Redo open document from navigation view
5f359a1dfffba7bd85ac80c39aff6897d9b39fc3 Utils: Start Environment/EnvironmentChange consolidation
bc87abec329eb15659367385999a8b94ac79b90a Update perfparser submodule
598ffc3b1c8cddf2381861e8d6f71b9b2050e66b Utils: Fake root info
d36ecb23dfeae01935a8331ddcf517356493b3e0 Update qbs submodule to HEAD of 1.24 branch
565c56cd4a835a0948b60330ade9468e4dfea667 TaskProgress: Make is possible to set id for associated future progress
ef065b8807722b6ee15551fcaad9058759eee8de TaskProgress: Add setAutoStopOnCancel() property
9429f710da813205ffa0fc798fff0920f2c1161a QmlDesigner: Fix missing validation
0d909c353c54ecf2fee2d13b7d2fe19eee01591d Designer: Update C++ code model on an object name change in designer
b4b260071587d681c78768a7a84807226857b081 TestOutputReader: Don't store test process
51fed728e1d351ac6532d63d608af2bfa59dbdcd TestOutputReader: Report results directly through a signal
bc3ebef7ceb261867ca57a3f8cb1abab2a46fa56 AutoTest: Use using namespace Utils more often
d05c5b7d07194b7c0fe938e88e63cfaab9ee5f4d AutoTest: Use TestResult as value type
3ff5661327f5c9df2c60eecefc3ebe178fe79f0d Move some FileSystemWatcher usages to FilePath
471e67d1a6b82e331eea137c1768520b47111289 Python: collect pip package info in another thread
b05a34b81e77032a381145a019ba5be1c15f62fc Debugger: Don't try to iterate on non-existent subprinters
585a4ad92a518f45f2771f22aa08de1c90118b00 QnxDeployQtLibrariesDialog: Add more processes into task tree
ab4f8b049e5c190a32e634bf527103748cc1f7dc Qnx: Split out QNX Qt upload service test
f46af497e149e85c08c46977f8bd6926f85f70f4 Translations: Prefix translation contexts for libraries with "::"
cf50f3034e964158ab3da0222e1a4e7ece27591b Squish: Use new approach for recorder
026d04d2808378617a192354e1d4b020e4dba1d3 Squish: Extract common message handling
73e80ca64f0f5b6a14ee4e13f20b4e300c683765 Squish: Start extracting runner process
414b95cb9112a6a11a4e32b9b404d4b02371f998 QmlJS: Reduce number of for loops
982ae8e69015349fb0019b21b2af7d590551c899 Update perfparser submodule
1d73bfcd37d809d2095814d3b3a07de7c26097df QtSupport: Fix translation context
bc6fc26951e6a23a38d91dff2ea93191e2e97c0c QtSupport: Remove {Designer,Linguist}ExternalEditorFactory classes
66e932d28442ca3df13c7066980d968fe9b0b8db QtSupport: Move Designer and Lingust external editors from qmake
94dd5a157b365c453b7edeb1dd49f5d1e6754614 Utils: Let copyRecursively create target folder
1285b80c4015763cc6f83251ab824549856003de Utils: Use an enum indication the possible abort of an dir iteration
4cf0918b5c6119082129051c9d74bedf23bd77df CppEditor: Use more FilePath in BuiltinEditorDocumentParser
570fa165b9af1f7fd85763cc537100a503a3000a Utils: Remove use of one FilePath::toString()
bd9f4c3b8fe14de5cb5578f2dc23ba23331e2309 TaskTree: Init with member initializer list
8e35048782b6e8f97aa460f77d397ec31e405f40 TaskTree: Remove TaskNode's start guard
a89d1e5958ddd116821279a7f6b42da1a5e75a9b Build: Don't add QT_RESTRICTED_CAST if QT_NO_CAST is already set
f49f9b43da38be4ec0f46fe6cd6930467c7444c1 ProjectExplorer: Give Xcode clang higher priority than "bare" clang
2b64232eba45c2cf38b113ba69ab65fde67f4319 SdkTool: Fix qbs build
e9b177db7d643f22e8ceba22dc7a3d7e0513e6e4 Merge remote-tracking branch 'origin/9.0'
f8638019d44533eff1e6ed3bc238ff2c3d1fecd4 Update perfparser to master
f83038d2451033b096b8e9c55f10565911071ef5 QMake: Simplify ExternalEditor creation
26572bc9822679fb9817f3e7bf5e6982e17213b2 qml2puppet: Don't rely on FindQt5.cmake
18aebc785f80685ce46b45185f30833adf62098e TestResult: Change semantic for createIntermediateResultFor
f9090dab0cfa835a0cc116a554d47ce011580634 TestResult: Devirtualize the class - part 5 of 5
2b99ba1db767ae6950a86e538f28a546de6587ef TestResult: Devirtualize the class - part 4 of 5
01828cfd8e47f6c612f83680b65710863167a209 tst_qtcprocess: Add testPipe
455f9f691197fce65db4d472865f8f09fa6ef3c6 Update qbs submodule to HEAD of 1.24 branch
5fcf6fc5a47565d830cc7923c1ddf1b81d7a006c CppEditor: Add more details to soft assert
f74cb26598bd4bd1858faaeaeb3c9a60da401c44 ProjectExplorer: Use better FilePath conversion function in builddir
7cb74e325f3adf54972d5257ab328ebeccbaf523 Utils: Add FilePath::copyRecursively
d57dd8462ec6a5054e76faf23f99eb893dfd15ea Android: Use better filepath conversion for AvdPath
15f33422cb01e590e5912ae70a2220304afff7f3 RemoteLinux: Be a bit more verbose when rsync fails
2cbe7783ded5d7385400b416f42d164cf6a876a1 Android: Move strange macro to smaller scope
c93db0feb4c85544ac6850efc457c8441e179a68 Android: Replaca a few FilePath::toString() uses
b73d6f3be80461f7e72326d622a07f8d2dfee10e Android: Read NDK and api versions from modules/Core.json
06c4df37b9052b591a05d04ffc93ebf6f7edae17 CMake build: Remove usages of Qt5_VERSION
f37aa909ac56d84bde92a5ee6650d578cd871196 sdktool: Don't rely on FindQt5.cmake
4e29a1efde58dc600738bcf274b4e80987d681dd ProjectExplorer: Code cosmetics in runcontrol.cpp
d6fb9754cad3cb87dab4a10007083a0d51d8c956 QmlJS: Proliferate FilePath use
77e7f0e3142ab4c11ea9781a6fd871dd282569f8 AutoTools: Simplify build step implementations
d995b650abfd4bbd8bde00deb75fde93b55c6a7c ProjectExplorer: Remove now unused RunWorkerFactory setup functions
83a86bc4fec025558295ca3ebcb056bb60a60ad8 CMakePM: Reuse EnvironmentAspect for CMake configure environment
567216bb491b336d4a002f6ebf7589d88d514751 TaskTree: Fix continuation of group with parallel limit
540b679c1424b299e9d6ea83ece75d43c67d2f9d QmlDebug: Tr::tr
112907910858d29edde16ccee13c02910819993d LanguageServerProtocol: Tr::tr
ad1ae7bd48dfe946059e1dded23c77521bc622a7 ExtensionSystem: Tr::tr
1c25a48393f03d6c03644eb340e6557f66284b46 Squish: Update getQtCreatorVersionFromFile()
86c2143353820a0bacae1c780286c2fbe9dcda37 QbsProjectManager: Make generated sources known to code model
8e3a22329da5196e4cfe8d326b135c7e370cf55b QtcProcess: Add closeWriteChannel()
34fd5b3cedce2c53fd4f945e9564d5f86edcce80 Utils: Improve FilePath::toUrl for local cases
64b5364cab5d1b13971c1a633d203dbae3e9b474 Android: Factor out removal of ndk packages from list
e2eab57a286ba8384bac622fa59c0a75f0bb3028 Android: Factor out parsing of platform version into a function
e7321e98b857384980b7dc55cfe6b58c38e028b6 ProjectExplorer: devicemanager.cpp code cosmetics
9c4e48b4b1c5eee84784b4c0385470848998c98d CppEditor: Merge orphaned translation contexts with ::CppEditor
9bfb289ceafe15d86e44234244ba7c2b8e7a18b1 Beautifier: Merge orphaned translation contexts into ::Beautifier
87c3f08e52005f764efd9995b9c3524542dbaeaa CMake: Move settings storage closer to class implementation
38faf098c8fd085833e194bb99524313b3ea43ba QbsProjectManager: Refactor raw project part creation
3d3241227122fa2bc6a5e57f8bc6210730a7d9f0 ClangCodeModel: Remove unused files
c3e44bb3bb9539cd65d25bbcf4af283fef332383 QtSupport: Pass empty device root for local Qt
9938fffe067d5077ee95e9485b3caa805c0488ca LanguageClient: close and reopen renamed documents
ea1d729669fe960e079fbb73f1f7f36aa1c1bac8 Utils: Move PathChooser::expandedDirectory() to its only user
156ef28d9013d17bec0ddd166eaa1119f8831ef4 Utils: Start migration of FileSystemWatcher to FilePath
5bbc7ea0f5d4d2ba9873c9521748501bd5871e6f Android: Add option to set the build-tools version in project settings
b1ef25b2085531100643138dcfdcb531078857df TestResult: Devirtualize the class - part 3 of 5
a7f0c8e81b7e73686034cfc2f337a5e2bb9e9f2e AutoTest: Reduce parsing attempts
2746b410d067c15d86489eb2fb65cf7edaebdc1e German translation: Git
270c0ffa3a42c8cb0e5fa93ccb3eaadf607e5047 Squish: Use consistent permissions
3873bcf2a494d74de108c14df110f6b356a422cb AutoTest: Add option to disable derived checks
978639b995cd5041698fba01bd0e9f4acbe2a628 Android: Use specific classes for run worker factories
9c57ed6bd28c4ec55fcd68cf49c17603b06c9b39 AutoTest: Only scan necessary files for tests
ab4689fbcb842c2a82422d9e2013b07c14d36017 Autotest: Add ITestParser::supportedExtensions
f15cae2bacba3616936a12b0e8f7bea0e660fce0 TextEditor: fix endless loop on adding cursor for selection
73c2c86c611579bd1d397b9823fa4efbc7bad687 Utils: Fix unintended comment
d518ba1acd702bc5daaa39281649b9ef2271afa1 Compile fix for Windows
d8d24cac7dbe0ac4bc3be56c0d793fcbe26abdd2 BinEditor: More Tr::tr
ea35f60633a65a22d7807d9c68f7a6a94e8c9bf6 ClassView: Tr::tr
457b53b95ca52cfc08d730d41a9c78f7cb34fc94 FakeVim: Tr::tr
a0eb8493b9816de162ce6a0674a7c25530020dcd Macros: Tr::tr
852a74c9c15b22e3f0ab62631517c7c4945069e0 Python: Merge orphaned translation contexts with "::Python"
f99a340ea44a7a9f10566c5d9ab429979c3f38a1 McuSupport: Tr::tr
cd4e116dffc8f7d0466673c8b0f96bc0e06c00c6 Subversion: Tr::tr
eada41a393d4d06d4f8b26fd67d088d6fe7fa1ff SerialTerminal: Tr::tr
817553f3e59d8b5c6ea7b465c4ada4987693e466 TaskTree: Merge DynamicSetup with OnGroupSetup
105fc92e19f7ac579be99339126551c747ea446d TaskTree: Simplify SFINAE code
b95fee74bbf1afe5d73af9da1241132601dcd7f3 QtSupport: Fix potential null dereference
0c9d5903eff1f9c1dd20e076602e2826ec10031b Utils: Fix crash in fsengine test
2336ff5570664dd278656d5362d749c3e84f85e3 Git: Some more FilePath proliferation
ec28990dd57ccc3dc2022d8e3e3aed9ed0a6396e TaskTree: Clear old storages on root setup
98ff1d32f92073cfafbf0daffec9f1a98f92c1eb TaskTree: Remove GroupConfig / GroupAction
01faf0843dd7b1641159b2b0a669ac80a6065558 TestResult: Devirtualize the class - part 2 of 5
2d491591e2e7b757ffa5cfcb597eb34960885042 Utils: Add FilePath.hasFileAccess()
6eb14c66c718245fb13c06a051c311ca451c3811 TestResult: Make createIntermediateResultFor() a const method
d03c4f77c97d301cfe85aa2295065e60a952ced9 TestResult: Devirtualize the class - part 1 of 5
c8b9e4504e2dae4451e7bc794cef7f8f2efc5463 Update to upstream FilePath changes
e592e0e83f95d63be79dac4e4503976a7444274d Python: remove outdated pyls install toolbar earlier
642c593481a3abbd8f3965d9e9aa8cbb7e519af7 Utils: Flatten LayoutBuilder related hierarchies
a37f2ae3a8b15794d26f9771be8e89402e8fc5d3 QdbStopApplicationService: Replace DynamicSetup with dynamic task setup
6bcee998a56343c46445ffaeeb1a6ac75ebfbf42 CppEditor: Merge translation contexts of "CppTools" into ::CppEditor
118b84ffd6975bc1c7b2687d0c0dab016fce3cbb Use simpler Plugin::initialize() when feasible
013ad1345e53fe9a52217a04445a479d90653a8d VcsBase: Proliferate use of FilePath
7dd2fe689d284aab152ece6eb8dd385437b9ef60 GenericDirectUploadService: Replace DynamicSetup with dynamic task setup
ad0fcc60d27953ea487ff63795efe8a0e1c554e5 ExtensionSystem: Add a less verbose way to *Plugin::initialize()
7e9e103c53fd64272d9d3c153b10c6fd4f2eb213 Issues: Fix ugly error text when item is selected
a20c4c44905b51b311bba319adab1b3c27f7c8ba ClangFormat: Fix the "Open Used .clang-format File" function
f0f0cf129ad34be6bcbbf0b693e7dde5cbbc56a1 Python: add interpreter selector to editor toolbar
d92be80610de0c519a21330cf0e9a24a9c63917d UpdateInfo: Tr::tr
3244819d3aae7c006eb6f0307ea5637565e08f2a GitClient: Replace DynamicSetup with dynamic task setup
d96fbb3e54f1b9bd52e3423f137ee4a6aed505e2 SubversionClient: Replace DynamicSetup with dynamic task setup
9dfd72017ce564898764c0d3b9669f3c3a14ce33 TaskTree: Add more tests for recently added fixes
6269c2cec8c3940dd83a564e397bfb70180d6b37 TaskTree: Unwind execution properly on synchronous done
e6ca18e19401474a28fdf64384957c00b15c8e5c TaskTree: Implement dynamic setup for tasks
42d7304d2b641b4dfd4343aa5071b2457f9e15d3 ClangCodeModel: Allow cancelling the background index
77054c2fa3918c9e0735e02b553a7225b5fe1fc7 LanguageClient: Tr::tr
6780a6ca5099ca4cbb424f25c1b73554391dee4e ClangTools: Tr::tr
5a5a91f7d2711470b7e2af633c663c426da1fa34 GenericProjectManager: Tr::tr
55161882900b459da6bc76e2d5c48e3c32ea4b34 Docker: validate clangd version
2e98acfdfd9ca7f0373b0dfdcd42d551fbbb089f Incredibuild: Tr::tr and code cosmetics
0fff2aa172ce2f22e4fe5e0227f6731bbd5b56dd QmlProjectManager: Tr::tr
16ec7205371577c5f3667d87530ca7d236b430b9 DiffEditor: Tr::tr
eeea7e8b6c46944583d3a5dde41faf25d6c6769c ADS: Cleanup dockmanager
89591b7d3dedd30ad5bca8951ad6073bf6f51840 ProjectExplorer: Make Node::pathOrDirectory work for remote files
cdc88b2571a4062d5753cd07924e30a3accb3894 RemoteLinux: Handle already remote local files
13cf1ee948d7164ddaa51186d9929cfc3bd20609 Wizards: Re-enable using qt quick wizard for qbs
117b6f192132b380c89a695bd791fa518222f074 ClangCodeModel: Tr::tr
0454e939e186350da446788ce5a3abaa0c98b696 Utils: Make all labels created by the LayoutBuilder
ca2979ca52d8b7c4121dd49be72f35f38fe1d37c LanguageClient: Do not untrack documents before restarting a client
9ff307253d2c49c1c06c568a469bbcbf1ad53945 ProgressIndicator: Remove fwd decl of non-existing class
7b5c5b774f9491389ba6d48dd138011f51efc60c LanguageClient: Fix renaming with pre-set replacement symbol
e9320a812252b05a6367462fea698d35f9585eae Fix tab order in "file deleted" dialog on Windows/Linux
534e1f34b2119529c8adcb5efa1afae8f05dc316 AutoTest: Only write values that are not default to user files
435fe5e4db2a1fca710727e46f514eb50a5b483b Adapt to upstream change
41294e70b4eacfbeecc9244ccb017ef6e01e4cd8 TextEditor: prevent crash on updating the completion model
d9de9db6f8adcc12baf9b499358d7d386bffdc9e Utils: Adapt to change in FilePath
1db9135d6d730a770560b3bb44f6a0b2bb61e463 RemoteLinux: Merge tooling runworker related files
fa7dd2fd3bdb1fd427635c9d23d0a40b47fe0c9a VcsBase: Convert to Tr::tr
7e815270f1b0a7e9cefa524a6cb52e8f22482c96 TestRunner: Get rid of unneeded Utils:: prefix
39940a720001561b12184a7992410d8f4edd33ee TestRunner: Use QList instead of QQueue
fdebf0343de843fbeadc6a250a0c6b8a9aa10f94 TestRunner: Merge 2 public methods into one
c9f5098c07395b0f6c5dddbe0a98970148b6f721 AutoTest: Rename outputReader() into createOutputReader()
bad66c1fc29060c58172613f6db2e25a44085684 CMakeBuildSystem: Run ctest process asynchronously in main thread
ea41beb90ad1a66f86d70f193c1098409905818d QmlProfiler: use dedicated classes to create run workers
263c5e6b7c2ef5cd06e0750c2fe0872a214cb681 Fix crash on session loading
46e660c646c74a9c1838bf68f405af7f2f5ed11c Boot2Qt: Tr::tr
88131d4c930cc8bcff44c8cfcb2adba28916c2e1 Merge "Merge remote-tracking branch 'origin/9.0'"
4d71a24cb9cd6c10a77ea3b32d7117d616a27508 Welcome/Qt: Show featured examples in separate section
ad643fdd30047be8855cc14db7d9a78f8655d0d2 Welcome/Qt: Use separate model instances for examples and tutorials
e8fdfd9e2cda923585bb81b1b93ff48e454e44bc ExamplesListModel: Remove unused/unimplemented methods
6c6277bc7d47b4e98019a0f9d93cb5dabbcd3cbc ExamplesListModel: Remove direct access of model items variable
e3acf9262b610a3cd01c871366c748a6a1258db1 Make categorized product/example view reusable
6d79c5c2b35c8d0f033bb4c16443723c9e8fbf5f Android: Remove "ndkPath" entry from sdk_definitions.json
4f138c1a2114defe2ef43e13bde32143dc890920 Boot2Qt: Code cosmetics
bb6764d065ab0a38de19a804c40ce0ed510e6735 Boot2Qt: Use dedicated classes for run worker factories
561b815d7b22634ea05868486bbbbc068bbfbe22 Merge remote-tracking branch 'origin/9.0'
aff6c4b2d217b467d8cc213469d828e58c8f9a11 Welcome: Remove requirement for subclassing ListModel
ab9935af73298399c9a804d0c74c4444c5f47691 ClangTools: Fix warning
da31e72605fe8b03c0be5b2c2aa220a816f2323b Subversion: Fix unused argument warning
b5df17cee3867dda95a4b9a70d7e58b7d55de764 Utils: Use ReadWriteLock
3e14368d294400bf63ff3d8797de892466faf4da Debugger: Adapt to change in FilePath
b9aac89961f83fd572edc223ebd428f51e9365dd BareMetal: Adapt to change in FilePath
17b20e06224f1fdee61fe5477b0789d09d602652 TextEditor: Tr::tr
87e5ac74385ffcb8713510c9e243e2455cb17516 Subversion: Mask credentials in command line everywhere
edd3c16382e9ef65ca85217c5999714e867c99c5 Core: Remove duplicate translations in qtcreator_ja.ts
8d190acde900c391dcdac1ef4c8dfeb3c21f9eaf Core: Fix include
91696f8bf3059bc88b8fa5890ad0d2d19abb7b71 Perforce: Tr::tr
ae59898aad4845fe615d6aa9fb38b6786417799f CMakeProjectManager: Override 'Open Terminal' func to open build dir
0a67f912ebd5a7eb46fa3632559d8b03d20a5b04 Fully qualify Tr::tr calls for the cases where lupdate needs this
015b1ee53cd381ec36f6ed3d143bdd8f0d87f40b Core: Tr::tr
8f8150db2d547bc5a7c783785128a7d45de615cf Squish: Add common start function to process base
01f85a1e1ef1f8a08cbe8b38750f770db0cdd59c Squish: Make static helper to member functions
3d7baa65f9844e32b8875d1d9fd0c907d759ce52 Squish: Separate server handling
bef44699a3feb22c4f478d050fab476fe84f6f11 Squish: Integrate query handling into common state handling
ee24d1264bfb66923368b00709d0fb8bb355c890 Squish: Do not queue xml handling
46d13932e3a63a0c2b4331ed98a589c8a0fa3b35 Squish: Add lost status text
58b197e2aeb9466612d18307062ae0eab4e1f754 Squish: Move recording over to new approach
3de8d194c5cd96de93ceb4165eb401ee38d6fade Squish: Move normal run over to new approach
daf0e062e1d6c1c2c7ec96d366b041b42536ac0b Various: Adapt to change in FilePath
7b74432fb1041c5153d80f6a64eedaf7fe032a0e Utils: Adapt to change in FilePath
15cf2ecf258d4dedbbbe329bb3cc81ffd49b2f49 LanguageClient: Export ProgressManager
71d18a1285c7ffcd426bb676a5f2089990f77bfb ExtraCompiler: Start a task tree when modifying ui file
f939f968eae18c25565681c58a3a84ef3b4680fc Utils: Consolidate Link::from{FilePath,String}
c527fc4fe48e2e343e02bd34d82912428124f5dd Android: Fix extra token
42c3e88f95c7dba17564cca1b82b437867672eac ClangFormat: Move the functions from clangformatutils
32d71c6da708662234322f671b819edc2fb60f7b Android: Move Apply and Cancel button to the button box
f920ee217ba960132e49ea43ccfd659caef0b4f9 LanguageClient: remove unused code
fabb53c7286334e5c098938729c60aa016e6dfe5 VCS: Clean up code for vcs toolbar
43eacc1ae5b385cff46ddd84aabccd44f37b343d VCS: Add a button to open settings on Commit Form
6a1a6d85b87e634542e36fbf7c5a0bbee5f30505 iOS: Tr::tr
8f2d48e8a350dc8e6bd87f62c1d1aed760f977ca ProjectExplorer: Tr::tr clean-up
5d9912b058104d815948080e91862f2f83a60cd0 ClangFormat: Remov unused functions
7cd2026fa2d887f4a3694ff9c3203686bf418d5c Utils: Re-add some safety net in FilePath::setPath()
cc89b79bc25634a59bad89c571bf9761a1de0650 QbsPM: Fix handling of executable path
f7f9d5d40588fbccc246c4691bd238c5de800351 Python: Fix crash on switching interpreters in run configuration
d96c7a2d0d4f678315c476dc973b413bfdfa2df1 CppCodeStyleSettingsPage: Reuse Layouting::TabWidget
c5f7f5ab0aab672180714cb172818aa3cd394c90 ProjectExplorer: Tr::tr
e373fe6aa91f455c52b4fc390e600c4116871652 Utils: Avoid temporary strings when hashing links
514a62b9b26fcdc19d8bdad5d2db22306304c055 ExamplesListModel: Remove unneeded override of data(...)
2f70875ee3e5d1ef51216d5594d098baf968e3ec MarketPlace: Fix issues pointed out by static analyzer
237d1b2c8ef6c52e24ff4209da3a6e708a8beaf8 QmakeProjectManager: Simplify makefile extraction logic a bit
cb7182ee442e57ea4fbf60db10ab4e08499e30d6 Qmake: Remove  ArgumentFlag::OmitProjectPath
4a217001755d3a5cd2e2ac0eace0ee6f3ef3786f QmlPreview: A dedicated class for the QmlPreviewRunWorkerFactory
9256340e5bed3a57b8f4cdca36659348211697ad CMake: Fix missing path to Ninja from Qt installers
c151835834938a9d71895c7fa3c20818a26aa5bb CppEditor: Rename some ts contexts to ::CppEditor
b7efb22aa9f28f2c4dddf155fac9dd1b35a9a785 Git: improve "The commit subject is very short."
7ecc507b75eb05229a5c704b7953d527f5bb7cc1 AutoTest: Fix test compilation
9414083ed548935447f09ca9a1915bf7e0c0baa0 CompilationDatabaseProjectManager: Fix recent FilePath-ification
9438575beb5670d0df6bcb838624eb7e7e4d1d8f ProjectExplorer: Move settings structures out of Internal namespace
7f85a2f3319b3f3837a14f15b0f1e61a4313e706 Beautifier: Tr::tr
2f144a56cd445535a026e694f6d02219aa9b882f ProjectExplorer: Simplify AppOutputPane widget nesting
a3109a53c036627f75e4baf4a14e4fcd092c09a3 Valgrind: Fix wrong include
34638aa497f94d51316156eddea961720990a172 ClangFormat: Add use global settings check box for a project
e7c536011f1f66e702b7191848c5e728ae6e50fe Prefix Tr::tr contexts with ::
eab76c5007c1d4aba8ee72f3493cc2fa6c292378 Differ: Initialize EqualityData fields by default
33cdb052ba865287c26108d9b126ab43f57a8631 FilePathify some testing code
274bb27cf3b8a5d888bafa069925e230bf5ae90c AutoTest: Redo handling of data tags with spaces
2ce413112b00fb29c979f1ebfaf55c690330ebda Utils: Push FilePath::toString border line a bit further
6585b3668cf88f447b14d72181f53e5368d68795 Beautifier: Drop a few FilePath::toString
2688a9629c21a8416c5d981dd0655ba8df901f95 Merge remote-tracking branch 'origin/9.0'
ccff024a66f9213b6ac811a098058061cd2f37f5 RemoteLinux: Use dedicated classes for run worker factories
1b6d434c9c045d09d3760f7db2025e29d37dc25c Coin: Update instruction yamls to run qtsdk scripts in Pipenv
44a9f5a63a09e5994542df6265b6813f44d29dbe Qmake: Use FilePath in QtVersion::reportIssues
ccb6176a33279c81498bf04aefc1143806c9614f fix compile warning
36779322667da646d17d31eea1594fd0aa559260 ClangCodeModel: reuse Id for restarted clients
7c35363cdacbf0c9f14654187f99ad0a717c16e0 Utils: Simplify part of OutputLineParser::absoluteFilePath
21ef25a0f5aa957857528861a960aeb1f2bb9180 Utils: Add FilePath.hasFileAccess()
b3bc499ebd506f7ce4fc4de243830e07fedd8428 QtSupport: Some FilePathification in the OutputParser
2df52c4b8bd0aebfa42667be9f7ad8932b1dca45 qmlproject: fix qmlViewerAspect
a661dc5db4e3e613f24add66b9f1dda452d94661 DiffEditorPlugin: Avoid code repetition
8900004b84f8204170ea12ef5735133de029a7bc Squish: Redo querying server
1ef673cb2e0748bfc2d9fe191fc3e40333bda9ba DiffEditorPlugin: Remove unused includes
f561740a0baeec8492a29664243fbeb17baf3e23 Utils: Guard against endless loop in PathChooser
3f528de7db07b735bea9676a0be43d3493009680 ProjectExplorer: Fix compilation with Qt 6.6
5d7aaf28cb9989392bd5661d1de5dff71b707129 CPlusPlus: Work around Qt SiC breakage
dbfdcbad0cf072deb314cedb49ee57387c0cf562 ClangTools: Fix incompatible class/struct forward-declarations
184fac8a27d71f6e20116acb03dc2f822798ed71 QmlDesigner: Fix build with Qt6.2
baa1e3c2e15af93804fa0f8b3158b2422f25b585 Editor: Allow to hide annotations for a group of text marks
6415eef6afd9295529be35fbff0bdb66df82e6f6 DiagnosticMark: Replace source field with ClangToolType
e7781e2a99bce1981ab4efab97e5df14b142238c ClangTools: Reuse TaskTree
1dad90ea451b763f8cf90ceb1d355206252c7255 RemoteLinux: Don't call mutable expressions inside QTC_ASSERT
a9c675d88d2a5eb030179dbd87103148ca56cc7a ClangToolRunWorker: Move logging of started tools into runner creator
64b9728a689c41eed376ccfe6582f375a3a3158a ClangToolsUtils: Add isVFSOverlaySupported()
5dec97ea4121a8253f6d89d99e5165eb333e50a0 ClangToolRunner: Add done(const AnalyzeOutputData &) signal
c181631de9b06654148c915479592cf2522f4a10 ClangToolRunner: Ensure the clang tool is executable file
60b23dca7518243ad9c5861dda572f4d2d352b34 ClangToolRunner: Remove unused executable()
c350c87e9e2dac54cf854bfe007fbbf6ec741a57 ClangToolRunner: Get rid of ArgsCreator
516f1f12b6fb16f9c019b0a36fdd1c202dac5ae8 Utils: Show hidden directories in non-native file dialog by default
211d6507c338f63ebcf1ed58a3206c565bc6cb5c Merge remote-tracking branch 'origin/qds/dev'
1d2c8c213dd160d6369878fcb3bda6477ad2ac52 German translation: Some individual items
69a79010d6eecb9411385ba4000dd0d088963c54 ADS: Remove usage of FilePath::toString
a61f8b02d30d2dfd8716e2ed7b7a23d287b68677 Fix build with Qt 6's QList under QT_STRICT_QLIST_ITERATORS
a0d7b51cf53079775544b869ec8b3991e36addde Utils: Remove FilePath::toString from QtcProcess
f7bcd4f574491211d73752fb953e5af099dcac0c CppEditor: Convert to Tr::tr
b2163f672f8bc2a64280697492e349fa72ba0dc9 QmlDesigner: Add designer icons to the context menu
85d067af916346d50da515d6f636b29b5dd4dc6c CppEditor: Inline cppcodemodelinspectordialog.ui
1e10161bf20914ec7474e2d3a3fb044996816a37 CppProjectUpdater: Reuse TaskTree
76260f80387a2a86f76859d624c8469fc0fce98d Debugger: Port away from deprecated getargspec
0648cd49f9f95ec5f1238b63718aac21cee932fd Debugger: Don't split top-level name into letters
f93ad9091f96e5b9aa0c184e5ce04024cd2f2c89 ExtraCompiler: Get rid of setCompileIssues()
c99ce1f455189864de9a2043730f704d7b024abf ExtraCompiler: Expose TaskItem for compile task
c79419402790ecb1a0b27a77740b416c8ce6ac5e Locator: Reuse TaskTree and TaskProgress
3171064ee27b9c232bb533579119c146598a4a32 McuSupport: use a dedicated class to create flash run worker
22da0f2fd660ebd916a140d1d3a64632bca7cc35 ProjectExplorer: Try harder to find a project for a source file
e78f0b5911cace8b1f7cc0c2dad6ad8a5029eb9d LayoutBuilder: Turn Splitter into a LayoutItem
11569852d4f219e01a155d6b728ec6f35fa21379 CppEditor: Get rid of an unneeded use of std::tolower()
394caa1feeb3b11e989ca62e1ae58fe3bb0fb25b Allow combo box scroll wheel when pressing Ctrl
14a70e151432be26108bea07feaebd0c2d2a52e2 Clang: Use multi-arg for strings
398b5656b4c17f15e107373fa8282efeaa5c6572 Utils: Use multi-arg for strings
65567b4717a1280a8c25c5690148067cec96011b QmlDesigner: Fix value types in property editor
6fba34f5a73c3df2c8b23ddc1fff8b41bd4e07ee QmlDesigner: Fix reflection in ColorEditor
ed3d75b0449cfc9b911cd2e546ddb55842267f1c Fix missing include
fce9583fd3207f1639559972f2567bca2077c8e2 Autotest: adjust actions tooltip case
711584bb3c444725183c0c2a17c9cb10d9e41198 Make AnalyzeUnit a member of AnalyzeInputData
4a897e0395d436fa7109e43a0e2781e5676e81df QmlDesigner: Enable sourceRect property
f7ef0c31e8868ddd2e413de2e5644b09e752ef63 QmlDesigner: Add QRect to node meta value types
9df7a6bdadcb5804041b18c6f9452a06653cd454 QmlDesigner: Fix SpinBox dragging
d4026287d4210413d9217733c330a362b35f2eb5 DocumentClangToolRunner: Remove unused getDiagnosticConfig()
b87f0519e300c86ded0e4fd52379b0df0d996402 CppEditor: Use FilePath for ProjectPath::m_sourceFiles
887db6b419b443bfd7f1fd7d95632d6ad18c603c LSP: implement call hierarchy
ac5db861293a656f8ad6e0b97d4213272f1e81e7 iOS: use dedicated classes to create run workers
91b527427612d8a898746d13ec48a6fb15c1d335 ClangCodeModel: do not highlight tokens as Text
c6471341f6c9001a84463b070d9566365e680b39 VcsPlugins: Use multi-arg for strings
db28ee6ec5636811f59710f127fdc317a45ea2da Squish: Fix logging output
b4cd975b083582f08b23d8de531f218e0456056d Squish: Allow retrieval of suite name from SuiteConf
017ceef3b0e5874575aa39e8a902cd54dbda55fb ClangToolRunner: Make overlayFilePath a member of AnalyzeInputData
511dc801b51022ccfccb66d54de919f6c31e07e8 Squish: Move some enums to a more central place
c7b60e7d6b098a3b5ea343f7b3c9b1023247a3a2 ClangToolRunner: Flatten the class hierarchy
0139690c2944a158f6de5bb56429fca8a2ce6b75 ClangSettingsWidget: Reuse ClangToolType enum
1ac69b69fce4c5a1aafbc4bd587bf797e5da0498 Squish: Extract server into separate class
c4882a5a7a051017ab0796c7f7b336def94138f1 ClangToolsUtils: Reuse ClangToolType enum
0ba3dbf1bef532b92fcdc15339d89f63554e7fb8 ClangToolsSettings: Reuse ClangToolType enum
7ef8d8313d4f0deb9cb6c3f1fbb103f9c0d28267 ClangDiagnosticConfig: Introduce ClangToolType enum
c4f6fd5dd2a161e88d619555117bea89620c27f3 Python: Remove wizard for dynamically loaded ui projects
c4e9616d27b42b62cf18b0593c9719920d294950 Core: Support Env variables for "f" locator
b16c6495a000fe4793f3517c6cb1c6e516480f8f Docker: Make sure temporary paths don't duplicate
b39010af791c8c955b434ed3a78fe34604b344f3 Utils: Allow FilePath::onDevice for local paths
647287ee0cb3ade4812db04cc42b5107ea44a0b5 German translation: Debugger
8ee6b14f00ebc1c0d113a2ee092cd0742450fe1e ProjectExplorer: Fix compilation with Qt 6.6
ff9c170053588999670df83457df5b912ea1d996 QmlDesigner: Show metadata of assets when being hovered on
b02b60800e49ad1c67b4a21fc89c98d2047b1301 Merge "Merge remote-tracking branch 'origin/9.0' into qds/dev" into qds/dev
9eb2a7bc5b7da67ef153e8070b08a68b65b57a29 Merge remote-tracking branch 'origin/9.0' into qds/dev
2c65bfa0ea54316eda153fa69cf118a56fdb6543 Utils: Fix FilePath::withTildeHomePath()
91998cd80aa4c248c250276053a6b1c9dc032168 QmlDesigner: When changing material type, copy base color
03eb5daf8dcab98c7ca543abdcf3d43d49ec4a6c QmlDesigner: Update Tooltips for 2D Geometry Section
0dfa542beed4fc399c545bf09f7032aee1af0d93 Merge remote-tracking branch 'origin/9.0'
67733f1eddfc5d773eac11e8358f539da5d7b811 Qnx: use dedicated classes to create run worker
2bb029c7e3f123b976df81fff9f6c22ed3b73576 QtSupport: Fix build
1c480899790c2f2654977818164cddb86aa2c440 QmlDesigner: Fix rotation block property name in puppet
f673d3d0b02722e90b7a43378bbeb765719b67dc QmlDesigner: Update child nodes in navigator when lock state changes
82c1a3a934fce4c081ffea314ebe26a49021dcce WebAssembly: use a dedicated class to create EmrunRunWorker
57adf73a893849a30d589148a05be2fcf9b18797 BareMetal: Use a derived class for debug support factory
4ddd28ae22f02c0e6291181184b5290e49be7ccd Utils: Rename QtcProcess::readAllStandard* to readAllRawStandard*
c2788b0f053015f08aa979c93ab920303a28ebd6 Debugger: Add missing space
f08b5727dc5947193f15353e10f798296e385324 ProjectExplorer: Introduce and use convenience factory class
c7884a2b171470fb7f8821be7ed6a46a2db3d851 Qmake: Enhance remote parsing
0a0dd9ea31d03fcc57f51de724c32e8ca3fe3d3f CppCheck: Tr::tr and code cosmetics
677fd6ba9a91add241447a067c65f46c61b2af94 Utils: Fix compile with MinGW
f5dce50dae88ae8415652525e6cea17f3f00af11 Conan: Convert to Tr::tr
f73e787bd19e4bb4368499f2b107d7c69208c50f ExecutableInfo: Share getClangIncludeDirAndVersion's cache
7684571e108f5d7a803944a1cd78f17b61a29c35 AutoTest: Fix handling of data tags with spaces
6e7c257a8cfec316fc863119716f4ea7dd66e831 AutoTest: Fix checked state for QtTest
8d6c16863188b26373a72e4c3c2a775c01fa1fcf Fix build of Designer plugin
a298e11e0c3c33a828519578179534ade1898430 ClangCodeModel: map compile commands json path to server
66c08a824d6d6ff65251afbf93c49e029a0ccc35 Utils: Rename most FilePath::{from,to}Variant uses to {from,to}Settings
ca4af940b1f8edf6e565a367d005981020b00fdc CMakeProjectManager: Provide generators
68b7276f869e007d48bf9a2edc7d97c7e205b333 CMakeProjectManager: Keep using Tr::tr
4e9c1d126c6f09b8813bc2ebc6cc195ef8d2c01c Replace GPL-3.0 with GPL-3.0-only
aee7afd50ba8bd91b1191db6f81b1bd3d8a068e1 ProjectExplorer: Improve error reporting
ad2013eee5c2a31fe9ac323cf0beee1c8d7d5440 Utils: Enforce use of semicolon after QTC_ASSERT_EXPECTED
bf9e2110e6d05ba949bedd8bb0e3b6d9f267e3e6 LSP: Use localSource
a9c1beed7a442da051b2e457940dda97cc7886c5 iOS: Cache xcrun location
b6bd59fc5722dcf17cd803686423aae1de90eb96 CppEditor: Add soft assert for unexpected semantic highlighter state
f5e4b7bd9f834e8ad57e84400bcd6676e14e3215 Android: Remove unused function lldbServerArch
538a906912d9005ad74b753555a0c4fbf4275e60 CppEditor: Add missing include
4e3eb5ab51336d4e7ddd777e5795e1ad3be81431 Merge remote-tracking branch 'origin/9.0'
ef798ae4eb9d1f8ac6bafa2e2d3daf2c8c5293ad GitHub: Use Qt 6.4.2
d62d39642e9d860842b2aad808925cb04dc0434a LanguageClient: Add action to restart client to editor toolbar
20cbf4d34b5256e28139b15875f67d2ffdd3f279 QmlDesigner: Update Tooltips for Component Section
e193d02a723c98bb7169267241218633913904ff ClangFormat: Fix no style for a new project
13fa4d5c02f9627433637707b4391639d67c2581 GenericDirectUploadService: Use ParallelLimit
fb477d65675f661f19d75e2f4cdf69b0001b72e2 CppIndexingSupport: Get rid of createSymbolSearcher()
c3e0c387f55f0ba910da36964dcd9eb7bac855cc SymbolSearcher: Flatten class hierarchy
63783f22c3cc37c2d0f2b794bb3756eaa4529462 CppIndexingSupport: Flatten class hierarchy
0c1077425fcf7d3342ac49705dc684f512df1ae8 ClangToolRunner: Move some methods into protected section
bf864ff3717254855fd3acad1b3da2f9c1e869a4 ClangTools: Get rid of OutputFileFormat enum
b7731b58a8e64a8faeb799c3fd9e409e8780a5b7 MacroExpander: Generalize registration of project variables
df3ace009ac0371610337e8efee0299469b0ef6a QmlDesigner: Fix clearing node lock/eyeball
23fa784e9c90fcc26413012f861becf696503fab MacroExpanders: Remove legacy "Current(Project|Build|Kit)" variables
37fafcabb23eea4fc22d3f54f714bf0ced297b94 SymbolSearcherTestCase: Remove unused toString()
a2d21502614f18a62d73a27fc211deadfda3cdda CppEditor: Re-use Symbol::filePath()
8ee463f311d2a868f3df5bcce8cedae8c2e0c06c Android: Remove extra spacings and margins from AndroidSdkManagerWidget
f0540f8392dc49c63908a196b71c1c4ae6607599 Docker: Convert isValidMountInfo to expected
2cfc981860a0e68b8b066d69272171b38096f1b3 ClangFormat: Remove setPath() from VFS
a04b4334894cb6327b510765a3b5b3a25a51bc41 Android: Remove lldb package handling via sdk_definitions.json
1802ad77a70646e9b0be498aa24e7885c17b6871 LanguageServerProtocol: Fix compile
deeda4c9edafa685b2e1d4740c799482fec5da75 Android: Update "Android SDK Command-line Tools" version
a3153c535d7e9e64fab530d86e64ab76ef4988e0 CMakePM: Add default kit configuration hashing
b6208ab34aa53280f06622edf1e3aa506aed96b6 TaskTree: Introduce ParallelLimit
fbb8d94e55bccfda87e97d4ad203575bf0ab88a2 Utils: Use hidden friends for FilePath comparison, hash and debug
1874906ce131f23fba90f893daa266e1828204fa Utils: Make CommandLine related operators hidden friends
bee489f9e2d34f24d9ac2dc004fd4e576637af3b LSP: Make hash and debug operator hidden friends
34ff9c97e60fffda06c3cb5bc87eae7491b0025f Debugging: Add dumpers for libcpp types: variant, optional, tuple
b2fc935f338eb095e3e4362118e9b36f2a1687a9 Fix SPDX-License-Identifier to be on one line
56baf8c058792187b574cf988fcf4b313f527156 Remove GPL-3.0+ from license identifiers
126d1d009e64bc39529cd1a118ad8c22e9f7fdfe Utils: Remove two uses of FilePath::toString
33e8251edffcf96a9b4cb206ff5a7c317d25f75d Ios: Pass context object to lambda connections
1be357f4be18233700244dc051bee7d45f3ef329 Remove GPL-3.0+ from license identifiers, part II
1f4b6837baa390f5a7d570dfd2ac8a9290f27b22 Utils: Make TreeStorageBase related qHash and comparison hidden friends
ce7b865cf7a90f6187bcb9419fbc616f869baeb5 QmlPreview: Robustify preview runner a bit
c7bb5fcc37edcc2ae7de8c0377bbd517936325fa Debugger: Remove spurious waring
adc874f6902efd3cda953d21905e495119247104 COIN/GitHub: Use Qt 6.4.2
53f46e06433291386b2c20d88dcee52752d48438 QmlPreview: Have a dedicated run worker factory class
67195f4270b53e33dea704ded57ed96dcd4af54e Valgrind: Use named classes for run worker factories
15d6e1df1ba2129049bf7e5c25f4eea8004910b0 Debugger: Use a full class for DebuggerRunWorkerFactory
2f6ac9d7971c4986987dd8b84ab9bb83478ea107 PerfProfiler: Use named classes for run worker factories
fb14965dfe031a6b83e6aa928035125623adc19a QmlDesigner: fix for coreApp initialization problem and loadwatcher scope failure
dc6790767927c05332817f64fae1371561fe06cf Update the qml code model parser
4e58ffcd1397f85e368c9862564564f4e44ed403 Task/ProcessProgress: Delete private data on destruction
2f39b51bdc1f73e2d87cc641a8501fd04ee76b4f CMakePM: Make "Autorun CMake" a global setting
2a2455a6fa610944cb19a5cca78137b264e4d25f Don't call FutureProgress::setKeepOnFinish(HideOnFinish)
45c98836a97eb3158a06f0d60afbb1857b15bb6c ProcessProgress: Add setKeepOnFinish()
fdb9cb905b9a3e05d59a1bb220414ca2bdd5d9f2 QMakeStep: Use TaskTree for running qmake step
acd55c0677907c20cd340792cda28522b84c7ce6 AbstractProcessStep: Provide API for running TaskTree
ac93351c93f6fc272f293372f1be63e648be6ef5 QmlDesigner: Respect node locking in navigator and 3D view
c022f53b88da5bdb7764a7c68c9807bed54d9869 QtSupport: Consolidate Qt version macro expander setup a bit
4e0b4fcc924c919a6d16d2e6096ff63c2be00cfc QtQuick Application wizard: Add "QDS style project" option
be1c5cab96b060a0f22fb79a66a971b5c5b447eb Fix static build of Qt Creator
ff59de94e9bb3d4102424c3bcd12fa1e95e0837d QmlDesigner: Create dragged item to correct position in 3d view
4b237c0e9822feedf69e6aeb1468742a3cd05a72 ProjectExplorer: Return FilePaths for SessionManager::projectsForSessionName()
c1a16d51a64a759a5dae0d0971fd7a34ef54dfb6 QmlDesigner: Remove isQtQuick3DParticles3DModel
28b6c30bd8ffdb1bbdb61fe2369f39464e958eaa QmlDesigner: Fix lingering merge conflict
63e885e7a1778e0c548fb116a13d3fed84bf65b9 QmlDesigner: Create arbitrary type for effects to identify them in qds
ac9023e85108db20681aa9232c7e1d4ac828f807 Utils: Make Utils::withTildeHomePath a FilePath member
55fa109b5935725e8f4c0b1bbd286017b6f52979 CMake build: Use version-less Qt targets
894685d50132d5d3f9785df72aa38d1b4a7d7b8d Debugger: Make calling std::function details skippable
5e9f8b30d43b2cff083f525f7804d7de61352539 Utils: Add defaultValue getter for Aspects
ce1a547b6cbfd875d2996ee71b9aa8f7327262aa CodeStyle: Fix preview update
098c7176788a4a692b1c360e37c07b68a60ae5a1 Merge remote-tracking branch 'origin/9.0'
ea35ed9ffed341884c1465f09d046ccd5f37fa0d AbstractProcessStep: Merge finish() with processFinished()
68322003135fec3235f8685edfd76ecca602f7b1 Debugger: Add missing stretch
b9bba49275b915569c74d190a74abe30bb4ca7d8 Docker: Fix typo in test files
850d5e6a0690ce6b22d3bcf33913fe5960a1e44c GerritModel: Reuse ProcessProgress
330a748f17aeb0783bb7d6d9e8ed802c90b5d6c1 GerritPlugin: Reuse ProcessProgress
9320fb611a33bff132314e9e76679602f3d147ef tracing: Remove foreach
1482bc0ae1d6cbc4accfe9001986a23feab41544 QmlDesigner: Allow drag effects to only QtQuickItem elements
c81f5ba7d144d18d042963c11da4ff2cb160a8fb QmlDesigner: Fix resizeToItem with live preview
ea1cafa675ffa8268b4ae3be9991c25f2a43391a GitHub: Build against 10.0.0-beta1
ad7fe6fa1c0ba0b0231066801f46b9ad94d304c6 Utils: Drop commonPath(QStringList)
56b9f99d76d489b1cadd2b87064d74534f11befe QmlDesigner: The type name of Component is now QML.Component
9441865714e541b1e2a9f0e1ecc516a1320d850e Android: FilePath-ify AVD handling code
34c206c7004bafaadc0891e4a66d841775eee4eb Debugger: Also ship asm highlighting file
b81d3d5cd243282c2a060ea4ea6b35559e4c5e34 ProjectExplorer: Use FileUtils::commonPath instead of strings
ba42169e8a3cf03252dc6abeb327dffa0025fdf9 Core: Convert promptoverwritedialog to FilePath
a3d1ef4e89d86b82a523b8cbe71ed13e71bcbe15 PatchTool: Preserve CRLF when reverting a chunk
afe7d48b6c77771fb373b7fa16ab27f72627242e UpdateInfo: Fix size policy on "show details" list
0a74a1e99e37e6685e627577dd52d4f06e3c2832 Properly support relative paths in Link with Qt UI
48990c940062803aa4569f9462382b83a1537b73 Qmake: Use FileUtils::commonPath instead of the string based version
1b43a4a1bd1ddf4c2c2280a63a9c7c8bd4a91db1 Core: Partially convert basefilewizardfactory.cpp to FilePath
fffd732edc49adc328470cf37a4e18ab5a78be47 GLSL: Fix file licenses
38a6ab6cd6e53b33142e2a0d1ebd1dcc799bee7a AdvancedDockingSystem: Fix file licenses
6b2b52937d4252d63f7f80893001c63048856e44 Debugger: Check for skippable frames early
2afb31f371a95e1d3b17913d1f3ef2a629e0f603 ExtraCompiler: Hide some methods in protected section
5344bec59b344bcb46a040e8b6dbe6e263705503 Add support for plugin paths set by the installer
a986227c9529130663db41edec094c3b59e108ba Merge remote-tracking branch 'origin/9.0'
d2f1ac542f077ca3518a611f3246d5731d678231 modelinglib: Remove foreach usage part 2
e1ae96647d60a262244fbfb8dd43485361d55e74 Debugger: Save a few cycles in watch data
b00442e946e289446af2dedb8e875cfcc4099538 Utils: Make FilePath doc sources a bit more unitorm
b41abc94bb82e300c5f646328d99a04a2339d208 Fix highlighting of debugger disassembly view
15fa9e7096d69b06ab56dfa99754fe5d5ff5c7ce FileFinder: FilePathify
09df8dfe4450e1bb09da62bf6ce3efb153e5e74d Utils: Add a bit more and fix some FilePath explanation
e38d1f06bd81adc1b7c077ad164e24f722456c6f Fix that Utils::sorted could modify input container
5a4092106ec37b627a409cc6e343d8e93fa3886b Utils: Remove old MIME database implementation
7f748acc3eece087bfcf254bf11211454508dce2 QmlDesigner: Fix ColorEditor anchor warning
0a3f66c7905f465f1b7b8e3cb934f03a68c5bff6 Android: Consolidate AvdManagerCommand use
b02d1fd0e9a712bc32c5b3d1a9de38f6ed5a0ec7 QmlDesigner: Avoid warning for unused parameter
a5bee6e3ae0088525d8d843e6009a03ba04696e1 tests: Remove foreach usage
cead76f3785857bd401bbd676b6afb8ef1dd5d49 JSON wizards: Fix "span" with combo boxes
e79a0ab49b79f959467409069db080eff83f271e Editor: increase text cursor visibility on indentation depth mark
f0dba78f4837b33dd83450e841b39aaa233da301 Editor: fix crashing on updating snippet selections
2e52f4d8ef3145e54f1d13ee99766b8d2e6723b6 CppEditor: Work around an endless loop
180c990dffc8f872b279d337019ec92e03c6206b Docs: Minor fix in XD QtBridge docs
bd679b61d119ab516c907e134dfb146fd609fcab German translation: Fix error message when installing plugin
a3c1b35475965f78bd39e050ae5dbcfcab582ae3 SquishTests: Update tst_designer_autocomplete
ac32a163eb5e6d4c504bcf72090d814becce9380 GitHub: Update deployment target and architectures on macOS
4947c5c6573584eeec6bbbd01080950f913188d3 qmljs: Fix warnings
4ea5a7abecbe125f933ceff57bb5f1a1b80935d7 Utils: Avoid one case of passing an empty path to isRelativePath()
371dcc3f26426be53a239e7cd4a103d4e31936d4 Merge remote-tracking branch 'origin/9.0'
b8e195d22be3c7d5f97c60f7adab39683846672a qmlpuppet: Fix build with Qt 5
5c34e9922e49e4da225d4b7774b2e66518316087 Doc: Describe "rr" and "sr" locator filters
9a335c9f670fbddb3200df8f8bce3fc17f7acb39 Doc: Fix typo
c342d3f32165d77538b69392d6107249af37d7d5 Doc: Remove confusing word
803fb4ce77d5b75a9cb542e6bd160f5396269d65 Replace ANDROID_NATIVE_API_LEVEL with ANDROID_PLATFORM
3c281e315b932f7598caf0607fb4d89348f27ac0 Merge remote-tracking branch 'origin/9.0'
01a4f47781686a497745db0ffd0f4d65daff0ff2 Build: Disable cpack for commercial sources
cd496c4fa767321c59a261bfc703fe175a1c559c CMake build: Add missing headers to be exported
1b1609ce277db8e72b2a4649b18702f0c0b5b548 modelinglib: Remove foreach usage part 1
f2956ece11f96d4a7fe9a7e82eee07c286452e89 Merge remote-tracking branch 'origin/qds/dev'
0e063ecfca92525c685555dc26278131042dfa35 Doc: Add second level to online sidebar TOC
be89f99ecc631431c15df970112f909eb38114d2 CPlusPlus: Work around Qt SiC breakage
2aff1282c4a9a6a4aa50c742449bea3924b4e9e6 Debugger: Leave everything in qobject.cpp when skipping known frames
ef3aad1c9b38bc2e66c41d32dd863a22e5d70bd6 Debugger: Save a few temporary string constructions
d5cf9ffd375e23dde4c4e51f4de7590dbbe64bf3 qmlpuppet: fix standalone build
9321c6ad24042a256ac44d28e5263e45f0fb4a16 qml2puppet: Remove foreach usage
9a7f152b9663c94cba6de1f7ecdd77d4bc37b1ba QmlDesigner: Update Figma Bridge setup doc
5d6765c08e7902ce777db64a16ac6107ca0ee496 changeLicense.py: support new license style
f0556b08b825ef92a3549f0581a441678a090fa1 qmljs: Remove foreach usage
f396b0b742e13cb4ee18dbb795ba9a3772de939f qmlpuppet: add .tag reading for GIT_SHA
01b71195f06e6dc45664c27b171a22ccf8b48070 qmlpuppet: fix include if crashpad is enabled
a049dbf8d31b9d2ee9b019528aaf20814e6baf4f QmlDesigner: Fix Navigation event filter for some Linuxe
e16af85f32045d6751d6588fd681eb75cfe2590c qmlpuppet: fix Qt5 builds
74da3abb326f2a83b1d94e4cca0b958aac54be62 qmlpuppet: fix crashpad build
50ebf1f82423e5586f8a850c8eccd6d5610a0789 Utils: Show macroExpander icon only on non-readOnly StringAspects
9eb3cb1accbd43728b6c884fa496192d57a4b1c8 Android: Remove Service management from AndroidManifestEditorWidget
3f340616d5ee75f426366f6e5bdcd3e85344dab4 plugins: Remove foreach usage
d77cdc88d7702f9755eeec470f9d0cc9e23426a7 qmlpuppet: fix build
1262c68f2ee22c7a13c000002ef7d918c6440e69 qmlpuppet: fix standalone build
f9a0b325d715fad239aebc133e2c653cfb3964d3 QmlDesigner: Fix the right margin for the AssetsView
2b97f69796f05d4d9d4b6745c4fbdc5d928aa001 Debugger: Merge GDB and GDB Extended option pages
c4cc5768255f15c566418a7ffaf924344d0c5f18 QmlDesigner: Create default effects folder in assets library
44d2f1510895fde649d622d92f11a1cc4ff33502 CMakePM: Update configuration failure message
0bc9d493aae52bd3c91d032282abd63ef2eb4335 Debugger: Move backend-specific option pages to the end
d63b2ca51f4a48358fc7c2ae635a88ac60beda38 ClangFormat: Parse clang-format file when needed
e4af787ff6c301c95851aac91796412b1d3ffe35 QmlDesigner: Add image info to content library texture tooltip
f81724f45819ae6c08b8a9d0390b1eeadc9ce4df Debugger: Make android std dumpers refer to libcpp
0cd9df503024089c671aeffa2300dd86cbdd47c6 Merge "Merge remote-tracking branch 'origin/9.0'"
33eb5ff391fd10a56ea09cc787c24c2cc4b78fe8 CMake: Move CMakeInstallStep definition to .cpp
3f4c3a3db448a5ddc23a317c95478b108ccebd0b Merge remote-tracking branch 'origin/9.0'
68e15e613f4e3a20a055ad8a1ad0806c742d0770 Remove unneeded .pro/.pri files
77dfa71f0618278ff974d0f274bb87fc36c91077 Deploying Qt: Clean up .cpp.o(bj) files when deploying Qt
c83e66d45f41dbcc340658c0052f9ead8aef3512 Debugger: Update "known functions" to skip
7e1fb3aaaa8c10e02bc4c92fc4d74a77770c9a41 Utils: Use std::variant for environment changes
68eb983b5df4ffae704b38a19149478e0f5f0f9e Revert "Utils: Remove some fallback code that uses a full remote env"
4a5359cb868dbf869d4a91ba5a942297b7d8b776 QmlDesigner: Extract asset type stuff from the AssetsLibraryModel
e7b4ccf608ee81b424bb687eb756725da83d082a QmlProjectManager: Fix starting .qmlproject on Boot2Qt
aa1690ec7bd6fdc7c7dcc4e5dd65c8a60afa4f12 qml2puppet: allow Qt Creator build without QtQuick
fb12660b0a097d75db2f05e2f3e7792ae84635cc QmlDesigner: Enable integration of the new create effect system
0887174727de7ee31ade97886ff5070ce7028354 QmlDesigner: Add "Group/Single selection" to the 3D context menu
970af9e64d1ef92987b366f4f2bd0604619de3cb QmlDesigner: Fix state preview rendering with multiple View3Ds
d53842d512e16effa1153a880cb6828c19067da3 QmlProjectManager: Re-organize fallbacks for runtime search
6a4123a96ffddd950d94268b49c47923a63f29cc QmlProjectManager: Even more FilePath use
5c7c102f3ef232431ce5fa2672637a5ea903da7f QmlProjectManager: Fix starting .qmlproject on Boot2Qt
08d4eab619a5648cec768c9ba8c02a9adeb55fc3 QmlDesigner: Create menu for Effects creation in the Asset Library
a0afd51f3f55df7617e4ed57f670d3f1b0d3800b CMakePM: Add "CMake Install" deployment step
2eb2aa13ae38dae6189531d87ac72d5062e60b96 Qml2Puppet: Fix configuration when qml2puppet is disabled
90b8e482c94e133203d1e26f88b4c002d546c671 qml code model: load builtins import
5cc77c4bf10464215b84c99636339fe0517cc541 qmleditor: Remove foreach / Q_FOREACH usage
70b33886843cde31ee4d6a46cad010e81885ae70 CMakePM: Allow relative compiler paths for presets
a7d8c4889ef6b45c3f7fc937c0109c9efe7793ab QmlPuppetCommunication: Fix cmake file for lupdate
456b8b69f9a0b6d8367a3ce1909df4cadd96cdc1 build_plugins.py: Create signed package for plugins
e42d24fca9fd6d7504e2d9a4cae2cc180abb2a03 CppEditor: FilePathify some of the refactoring operations
edd0e97ce12b9d2e1fc1cb8bcd34466ebdfc0a89 ProjectExplorer: Fix wrong include
cb187d79127c7293dab60c5609cbadc606d741e7 UpdateInfo: Fix long "show details" list
965fa9450c3e3424e5e9e4a1bec3612a66594059 QmlDesigner: Fix cancel button not reverting background color
bb37d782ca9fa4ccb56957a6273822ae393fbebd QmlDesigner: Optimize requesting an image info for tooltips
674aad728e3c92e6840702d6da7fc5142054dc41 RemoteLinux: Hard-code /bin/sh for use in Open Terminal
223bec26a1ea76ab0998bb25a7abe0e22421342a LanguageServer: Add tooltip to file pattern
0aeec80eeb16f9412647124c2ebe3c4499fb0684 Docker: Add Filepath::localSource()
0feeb37ef71d64e0848ed6e36854f019236ead74 QmlDesigner: Fix textures created from assets can have invalid source
3904539e0d7fad334b0360018ca3326640fc3903 ClangFormat: Fix crash when m_fileName is empty
2ffa843d404f47693325b7da0f887722f1f114dd ClangFormat: Make clang format checks widget resizable
3f554f38376781abd31faa3176a7909301065496 TextEditors: Implement "Follow Symbol" for HTTP urls in string literals
aaf2e6f0f780681a2f93108823fced585b7e66af QmlDesigner: Fix asset images becoming invalid after delete + recreate
e603116abb5d94b3b5df3988bd7c2f8dabc55bf8 ClangCodeModel: Remove unused variable
994626d1f6d4decffadae4c5557b457bbc2463ea README.md: Add/correct instructions for building perfparser
676a4c1ee3204b5239de17c8df384ca49dbb1c21 Fix CMakeFormatter
1eb73e4eb31e6b8c8e6d1c38b588c2dba6762d0a Editor: prevent validating line number in unwrapped editor
f87fccafc181db7844a95367f0253a0da1e230f5 TextEditor: prevent shaping text for indent depth painting
5eeef197435a8faf11b9318ce5e5d145adb53b29 LanguageClient: resend rename request automatically on changes
f9ecc8b57ae9c09dcb57085174f66ac6897b6efa QmlDesigner: Ensure all View3Ds are rendered when one is
76565fa8d76ca350a01700d5fd546aefa3b64452 QmlDesigner: add support for multiple qml backends
2e6437b8fe98765f219a93d1f3493cc47cb50e71 ClangFormat: Enable plugin by default
c0a692a96d54135189556f309cb89cb7cc98b513 Clang: Use clangd from build device
34aa8e9e19511e3ef4d1f18ed0601c61e7764fee PluginSpec: Add a "LongDescription" to plugin meta data
858d49611333a86dafac621d2116f937efc55e73 QmakeProjectManager: Provide generators for Xcode and Visual Studio
c8e120123671bfe25fb508508f796714e2b42622 LanguageClient: Do not send out invalid errors
a86679d00d2c95378126f6d86b5f6ac24bb36427 VcsBaseDiffEditorController: Use setUseCtrlCStub for vcs commands
0f52acfe1cea8f1dbd053dac56f1318f902f1dfe DiffEditorController: Remove unused stuff
d59eca85770e1124ccfe589f534a880e55c1fbae DiffFilesController: Reuse task tree
df2d68eb4fe31bd6682459775f0406f39d8a3984 VcsBaseDiffEditorController: Remove unused stuff
2d285bc80875158dfbab77a37effedc8a1e1a693 SubversionDiffEditorController: Reuse task tree
b5ebc80370db306f48946b49b8d6b9b9e43ea650 MercurialDiffEditorController: Reuse task tree
817c13c9e13a145a98413e25faeea0bfbea519a0 GitBaseDiffEditorController: Remove unused method
c06c6be4ab6bba7efa4ef6c37181bc65871bfd6d GitDiffEditorController: Reuse task tree
36fd9cb9d523894698d691f7363c32dc2caf8018 VcsBaseDiffEditorController: Make post processing reusable
6de2b0cbb392167b0e0f9f00f3fad0ce564ffc01 RemoteLinux: Move RemoteLinuxEnvironmentAspectWidget
feb72dd0845b50cbcd5c79b4a1241e3953b3043e ClangFormat: Make formatting mode a project option
192605f01c307d8882e8f2e387841017da3986cf Utils: Remove some fallback code that uses a full remote env
51fe3ca59e878f4c87359e9777cd7057e760c553 QmlDesigner: Update component integration instructions for CMake
9f9c40b29f846f2a1bc22887d335fbb4e2581875 QmlJsEditor: Implement "Follow Symbol" for qrc paths in string literals
1faae7dec412b947f8a0a03c18b50e16eab83f4f Bump version to 9.0.2
31af32533ed4c5e204d88db371c929274849a7ec ProjectExplorer: Better fallback for displayed names
4ea60589bb6a6deb613ec92995e7cf47b1a358a3 ProjectExplorer: Allow opening terminal in run config
698a49597214005a25db3244b28bfef5bfecf3fb SquishTests: Use existing function saveAndExit()
8b49b091f7148a077e291e0bd65801870ed74d53 ClangTools: Run clang-tidy and clazy separately
01dc87b10001141b1516fe24316a02254b6e6f40 CMake build: Add hint for installing libdw-dev for elfutils
ca6b14cf01dbe180faf89f23fd35578de8387557 ProjectExplorer: Funnel environment fetching through device access
8b1233c11ef0b53647784d1d3afa2bd7fda833ad QmlDesigner: Fix mode switch crash
0b6b31b4ba7667226fb7f979f5d03a22c0abf7dc clangformat: Fix compile error
445c3624ff32978493e4aefa3e0b02755d491f72 QMakePM: fix potential crash when accessing deleted members
2f7263cd67c178ef9a122cf76dd4e1c4223a45ee QmlProjectManager: More FilePath use
a1711fc5bad3f1922d7600fb441c91c8b1d24dff Utils: Add convenience functions FilePath::path{List,Component}Separator
eeeb5f0aadbb9fc936278366d65f4a6646aaaa72 ClangFormat: Support remote filesystems
bf4a94d6191821ebeb1295f4a261459b09a9fff7 QmlDesigner: Fix dragging materials to Model in navigator
5787a22f699e4bc766d9d6099117c3167af23184 ProjectExplorer: Merge IDevice::{filePath,mapToGlobalPath}
67c2570d748efdbddf6f052023f725cd8aa3f49d ProjectExplorer: Add infastructure for build system specific generators
c476a1b807b25cc4a0529f59f3e13dabb0036e7b QmlDesigner: Intercept stray mouse events on popup menus
432c5cd6522d9c9cfe654f3c803e750c35dc1466 QmlDesigner: Update docs for menu items relocation
8d08409f59a9dbc1d83600431301b7900dd0a9aa QmlDesigner fix: do not automatically open example project on download
7e5ecfe8bcdf138292572a07386acbff1d491ffc Fix glitch in AssetsView when dragging files onto it
ea36472dcec4e3227cc2113439c7d7168d2623e5 Perf: FilePathify PerfDataReader
371e674967af97cf28e2425a0b98b80a87159f09 Git/VCS: Use ctrlc stub for process execution
3a90977da0e35429c953990c8dcee3ae5b9db418 Merge "Merge remote-tracking branch 'origin/9.0'"
b2e4bad49cf4e2458565a58fa53d9347e3af4d48 Cmake: Give a hint on the use of  libdw-dev for the perf parser tools
308a12b8338be650389786532714fe83364b8c55 Merge remote-tracking branch 'origin/9.0'
2d0456f08509092b2bd882a723b8b26b2143945a LSP: Support remote LSP file paths
75c43f926b95cb7ac546ec8929e6e759320e7d89 Squish: Stabilize invoking context menu
0b33a08af1e088670e561fad316f4dfe7fcd265e QtSupport: Warn if Link With Qt failed
3874ac236d1bd14f6904c03e63e88f065c6fe62e SquishTests: Stabilize tst_codepasting
7b073efe4aa94827e4d17c8dc8d21437f690a805 SquishTests: Update tst_QMLS04
bbd5d6c7aed1b98e5405d39ad42e928a00409b05 SquishTests: Update FindToolBar
7bd26571e44c960e6907e3c39244d27842d2726b DiffEditorController: Aggregate reloadRecipe
5aedb4ba563f4de30686fbe1dd18229ef57dbf6f DiffEditorController: Simplify API
6ae064619f4f3f6131493e2af107696a6d46c170 Add an option to style merge from string data
ce161d0b16f498636e3e3adf74c8bae603b4c76b VcsBase & dependent: Fix const correctness
042087ab1deb46da3288df1aae3bbda03bd7e433 DiffEditor: Simplify internals
ac7a582ca9769d641a1b7eaece360b6828ed70c7 FileListDiffController: Don't set startupFile()
973f74b8a0596b6104df38d891846400c33c8d69 qmljs: improve readability
f22a8a42a97746e38df126c7fa9aad6ec30b6788 QmlDesigner: Avoid infinite recursion
3066dce3b829f807a59deae243ce020c86516252 QmlDesigner: Show error message if Qt is less than Qt 6.2 in template
d0046ec4358585aa8bdf2ee13d61b0329c06e334 ProjectExplorer: Consider the form "-D key=value"
e3df0b90d8b9d6ff85ee265fb23352f1c6f28bda LanguageServerProtocol: Fix toPositionInDocument()
4723848be5e349b7dd42c37c8349734ae854c15a Merge "Merge remote-tracking branch 'origin/qds/dev'"
7f234ed768075dc14ea3b74f4531349851182aa5 Merge remote-tracking branch 'origin/qds/dev'
fdad6496f4f6034f77108d43cf8531651f36ae46 Fix language selection box
5935536b71ba5fa23a1063674d225e50bd03daee qml code model: load builtins import
249c3561cbccb978889b738f2d0ce8e41a1ac371 Merge "Merge remote-tracking branch 'origin/9.0' into qds/dev" into qds/dev
db4bbfdf4b4957225d571e9c25837ce297a4ddf5 Merge remote-tracking branch 'origin/9.0' into qds/dev
a3fe24d1920806e2307b754d42959ba3d891c5cc Merge remote-tracking branch 'origin/9.0'
c5668952aedd9680aee17208a904696f67f923b8 GitBaseDiffEditorController: Simplify internals
cbcfc0242e13a9319ff964b7b876fef830def5f3 FileListDiffController: Reuse task tree
1ee4b0e58337e90481caa715c652e42faeae7ecb CorePlugin: Don't call mutable expressions inside QTC_ASSERT
72e19f09193b7c85eebc064137a3dee0a25e1a91 Android: Don't call mutable expressions inside QTC_ASSERT
bd170f374852e03f30be1a085c349c83e7f90ae8 Ios: Don't call mutable expressions inside QTC_ASSERT
038847709c216f24ba31ae2ffa4afe33b633490b Git: Suppress all output for instant blame
7971a83730a883000821fc26adff45c9bd2f398a RemoteLinux: Give path line edits a bit more space
7b08e799136bd2729d2b4093b89e702532c9f07d ClassView: Proliferate FilePath use
2119dd43977d8d45a37028a8dabf050812152f5c Git: Do not show progress bar and the command on instant blame
bf060b426fb3bca7c59d88bb22c3ebc9c617d3b4 RemoteLinux: Use remote path choosers for gdbserver and qml paths
e46141dcd0692704b5f26d5669de2d28009abc71 QmlDesigner: Update the tooltips for QDS view Components
eca7044361415db47b62326ae3235d97d4cb7420 Utils: Add std::expected implementation
13c7283c0e3f8cb1c199bbc3ddb16bc5f99f5910 GitBaseDiffEditorController: Remove interactive branch expansion
85b645f144208ca47177e9ce165e8bdfaf454b89 ShowController: Make showing branches a part of show process
eef9cb458bfbd34a513222cf15f2e00ac93a0b16 DiffEditorController: Make it possible to setup task tree
041c59e90fc93ec9fbc82f849d89c53e865ae4a3 QmlProjectManager: Support running on RemoteLinux
4159c4b5d55c65d0759925e0dc8c63c60653b367 CppEditor: More migration to FilePath
9bac0d7f4c574a88455771b88f078c0c10dd9b32 Utils: Be a bit more informative in failing FilePath accesses
5cb1a29af25d9323668df4d383ae57ba93fb5354 German translation: Some cleanup
8e8afd3cf6964ded9795aa0ed246356ee29c70ff Utils: Remove FilePath::fromUrl()
af881688e5f4f28aed527c571289be23a01f7c24 Debugger: Fix name of "mac_stdtypes.py" file
89403daf4ef0db7fd8c4a5c8a61050d0df3a0364 Utils: Skip roundtrip through QUrl in FileCrumbLabel
dbaade3b47d067a72f7b61f6c0eaafa898155bc9 ClearCase: Re-use FilePath::removeRecursively()
e0b1f694e35d24ff4cc2741f1df0e7158790b3c3 Editor: Fix deleting with numblock delete in multi text cursor
75177f4c342220e0c0dbb3f73a261d8e1fac12a6 SquishTests: Expect some more error message
bfffa32f1e1338e17cd1a73d83f2e2e97afb1b95 QmlDesigner: Update the tooltips for QDS Component Components
f9e373bba7fc520e14b197277ad559765761a55e RemoteLinux: Add a per-device setting for qml runtimes
354f67f11a4ac7500e8397cd5c49d3a993202c52 AbstractProcessStep: Remove two virtual methods
9f53ba5795917935b1b64ca0bdf78a17956f5618 AbstractProcessStep: Devirtualize processStartupFailed()
d726615546dc4d6742b6715ae3fe4f8be736482e RemoteLinux: Move X11Forwarding aspect to ProjectExplorer
337d48ca9253b45e392068796974c0ac014c5be3 ProParser: Prefix mkspecs with remote roots
72751a9b98e48bdfe528b394f3303992fb1f21eb Git: Remove outdated comment
857ee29c1a177117ed44619a05ff76af6d93d813 QmlDesigner: Add support for dragging textures to materials
db7d7eb801140142edcca9dea272cffb65f862f0 QmlDesigner: Fix image data caching in previewImageDataForImageNode
34ecdfc68240768e69e123faa101588ae915d8c9 Add QtQuick.Window import for 5.15.x projects
ca023553b2d7d75442ba7cd6179a01d10451edcf QmlDesigner: Update the tooltips for QDS Basic Components
87d076d6a6d5a0bf6fe69665ef5e662f468b5e75 QmlDesigner: Fix possible build fail on some platforms
5527573a86c465a4e24a9537b5a3c2c37913b5f3 CppEditor: Proliferate FilePath in cppsourceprocessertesthelper.*
3d3bf4d9234e9a454a3347eec9b08323b070af0c QmlDesigner: Adjust for scene root transform when finding click pos
521f220efb7cfcab86b0378884d07bca76a24ea9 QmlDesigner: Add basic texture's info to tool tip
2a0a1181f9cf3f5dee7ee15d4752dc99119bf4e2 QmlDesigner: Resolve active scene id also at model attach
8d17762bb0fbcc12e20603e3daea29697c52cb57 ClangCodeModel: Use newly implemented clangd support for operators
1839de431cfa2c75fcb4e4a26bf8abdbc5a8cbee QmlDesigner: Implement "Anchor to parent" for 2D-View Context Menu
fcf70d69576f5849cf72573498fe025e90bf6234 QmlDesigner: Add checkable and checked properties to AbstractAction
ef8802487ed6ac6fbbf72f3794d7feaedadc5872 QmlDesigner: Implement new context menu structure
3e49961ae7f4f2b59390a29f4a4523ec33a684ee Welcome: Remove unused function openDroppedFiles
a2a6073b0ca7cde8e9e4ad50b71954acb448f369 Docker: Use FilePath::from/toVariant to store clangdExecutable
e7a24486817e4701be1e46b93296a29b5c1c27af TextEditor: Cache tabSettings
4d80daf71a1472f3dc22eb4cabbe6bde478c436c Utils: Use QStringView in FilePath::setFromString
81075b813c05b7b984001c8cb9e3dba9f44e0842 TextEditor: Fix wrong tooltip when split mode is used
430998d7698193fc41d807f164223c839b23c1af Update interface to let refreshing files in code model
763c76b47768a76e5e49f73b0aaf9d1f08b54c42 ClangToolsProjectSettingsWidget: Don't overload slots
92ab0c7f132a0514249814a82d617f683044e739 CMake: Fix startup warning
56e242c9f9aaab8cf5a25f41b77b26f9304ff0d5 QmlDesigner: Disable 'add to selected model' option when it's not valid
bc7eed8c62158c72500bad28dd4a9d8b779508e4 Boot2Qt: Fix parameter passing for make-default step
1e122fa3a330d406fce5d7fec8df3cd95ba2b6d7 CMakePM: Have case insensitive locator filters for cm and cmo
af60fb824118e0fafce3c2fd20f4985627b54740 Merge remote-tracking branch 'origin/9.0'
172066ed5681a2398f4f2551968a6278e861f52d FindToolWindow: Rename setCurrentFilter() overload
e5e31e573281e441846ab4f67c98eddc78f8425a MesonProjectManager: Rename addMesonTool() overload
4fc5483c74b23bb7b8927378951cec50410c3fee FontSettings: Remove unneeded QOverload
36c025ba2d3ec8c28be7ba6a7c48500b0fc08f0f Utils: Change Aspect readOnly default to false
20d81d3e61f9e5e4ffae04c4d4a69706899d536b CMakePM: Fix qbs build
131ecdd3ec455642c32aacb0497b73155355202d TextEditor: Pass context object to lambda connections
12f66d18ff79390bec5635eef397e84f10f37651 Fossil: Adapt to upstream changes
1cdf29a1e6587eb44058c29e5fd50782e60afc08 DiffEditorController: Make setReloader() protected
32e824c76257c21ee4a5d036ecb7bfcbf6664448 VcsBase: Don't return editor from annotate()
db85862a8c035093ca2c33b0fa9e90546b24855c Git: Reuse handleResponse() inside vcsExecAbortable()
305dc46902cb4484c5798c712690feb368962ed8 Git: Add CommandHandler into vcsExecAbortable
c2f8677e9fae366a56510b041eb7f1d6b57741ad QmlDesigner: Move actions from toolbar to context menu
ac2ca7244a06c335230ce8dad56367f43bebf70e Beautifier for CMake files
6c1545bcb3df22031663dd65807cd75410a5d94b QmlDesigner: Apply gamma correction on generated icons of HDR images
bcfaf00d0063cd549e0edaf17d70b9c8228a4dff ProjectExplorer: Add clangdExecutable to IDevice
d3487b1bde9d5814714fcaad3ef2e653aac336ef Utils: Fix FilePath::searchInPath remotely
067d02f82dc18441a2d7f6dc455e435b3c7b543b Utils: Only existing path to FSEngine::deviceRoots
d01c22b8057b5eeea5661777c6dd56a71554bf55 ProjectExplorer: Show near paths as relative
df622c9c3cb22268c0d21bfcecb8f5be9590afa8 QmlDesigner: Fix expand all and collapse all in content library
1caade7333a961389fee71bac1264af0162027c1 VcsBase: Get rid of RunFlags::SilentOutput
8d7ced7d83a487c0cd52327c2aa08b8ea18edefb VcsBase: Get rid of CommandOutputBindMode
e65e5243f8ed305d667a75881c14d01151e1331b Merge "Merge remote-tracking branch 'origin/9.0' into qds/dev" into qds/dev
834f89acf2bb6108f7410a57d5a72a37fc497647 Remove unused variables
944ba4a5be06393f887a06e0e22597e4ec90c407 QmlDesigner: fix adding effect to 2D does not work on windows
4ff34cf47febf13e4f60f498228fb75dcfc4dec2 TaskTree test: Use storage for getting the result from task tree
7fc99339697405fe1abfe6cc5cfd2d8bf195ae95 TaskTree: Add a possibility to setup storage handlers
dfdeb4d630e9678ee2449c73a35a44d9580d10bd TaskTree: Ensure the same storage isn't added twice
8aa7ec107245d8af78f6b52dbe3961ef3e2457c4 TaskTree: Add hash function for TreeStorageBase
cb8d4797b79986b452602d7d03f0991dfc305043 TaskTree: Add TreeStorageBase::operator==
162c8f71d3cd9ec7853ea4bbf8d4bc4c84cff88f GenericBuildSystem: Don't store unused expander
fc6b7996b8877532b3dbb1580e6e371a6d29eaeb Debugger: Pass context object to lambda connections
448471a3993d388f5c7edc175726a1b5123d4577 VcsBase: Ensure context not null inside vcsExecWithHandler()
c0f3ef9a823246ae0df044f153605d12752dd5cd ClangFormat: Refactor for remote file system support
26f07c62a0ab1b265816e32e1b99bae22d55eb8d Core: Remove unused Key
cd129309530dea116d4216ebeb2504ad19891a29 ProjectExplorer: Fix unrelated tooltip appearing
112835922ac896f1b25381e048fbb295484f1239 VcsBase: Reuse CommandOutputBindMode instead of bool
46213c82be82124be7fcdcb15bceecf2d174052d GitClient: Simplify vcsExec
0cdfac0cb5ee692576276393e4999d91a6689e25 GitClient: Introduce execWithEditor()
287a7c92689628bb05c8ca4a26289b49d8440a05 VcsBase: Introduce vcsExecWithHandler()
e990b828a99be74d8afd88b900039b9cfba85310 Doc: Document password protection function for shared online apps
c08317b5a673b7bb57de9a6801169975a3dc80ef Utils: Remove slash normalization in critical path
b3f82887a3ccc0a576a4720d8804ab99db7ae2b6 Utils: Change browse button to OptionPushButton
59505310a5b46e49ccd1db8e2e3e1d15aa1f3e70 QmlDesigner: fix QDS does not quit when closing it while Splash screen is open macOS
36be7b83757af6380aa88489963829026f79305d Utils: Don't call mutable expressions inside QTC_ASSERT
a6b17d127af250ae65566e121867071b55f9afe3 VcsBase, Git: Don't use queued connections to VcsCommand
533d9697356761e95bb33ab5188ae1b58a6bba39 GitClient: Remove unused connectRepositoryChanged()
7d573cfa0bf4f19631449b72ee859443375362e7 ProjectExplorer: Fix test crash
56450a8fe6839a5e50186ad3b92861a04c6a6636 QmlDesigner: Update application template for QDS projects
d0e24654f6c0f2128ef2c2ca6c003987661a3c9a CMakeProjectManager: Pass context object to lambda connections
9e7743494c7799d2ade7209b75f956125c85593b Utils: Use some of the FilePath convenience functions in FileCrumbLabel
4c1b3c863cdf90f8df62a11de31acfec03ed39e0 ProjectExplorer: Add "Generate" sub-menu to build menu
6d8f6ab39595350afe67e2649dce658613a46abd LanguageClient: robustify renaming symbols
55ba10be38767ba5d59170e51b212fef8e2653a4 RemoteLinux: Pass context object to lambda connections
d0c9bc76cbf10c47f0daef2fe26d1f16ec2cb9f1 QmlDesigner: Update textures sources on state change
b67201c4102630d7da9608c238747ff6be89f443 Git: Simplify ConflictHandler
f9800bb79035bee8dab908dce9816faa6779d00d ClangTools: Pass context object to lambda connections
37564d267bd1476753ab7cd31eafe6548a8d37e1 QmlDesigner: Fix Connections status in ContextMenu
636c9524f994f32bd20a4683836ad2acd0c6893f QmlDesigner: Work around the issue in QtQuick3D 6.4 geometry caching
b354421f27d86ffaff38782f3312d8fd844f7d9a CppEditor: Use FilePath in fileSizeExceedsLimit()
e70f909bcf01aed5b3369c0d307a10319b529adb QmlDesigner: Update texture editor and browser after source reset
5c86f58bdd790874a80f050c595e5f27fe540e10 Boot2qt, Qnx: Pass context object to lambda connections
c021fb5179eae952e1d46509536f6096e4fb165e Don't call non-const methods on temporary QList
8e7e1dd5f2e3d0179b9b49c2914a87b09006a12c Mark some virtual overrides final
42bc552f2be3b905f2499097e18826a8c179509b Beautifier: Pass context object to lambda connections
c5a60d2dc2cd38469647f2700beca767eddcbcda Docker: Pass context object to lambda connections
9e571931d05627ef7348295460b19c1cfede4a17 AutoTest: Pass context object to lambda connections
f6913ce7913c2f26bf346b000b61bdd0b0eef872 Squish: Pass context object to lambda connections
0ddc0eb8e95ecb3815f9ae40407d9c3fbef8cec2 ProjectExplorer: Re-layout target selector
b530f938baf566c678318533b94211fa2746951d QmlDesigner: Keep search on when adding a material or texture
5769fd82d3cab9a0f2eb30a1cfbbf20aa09d3859 CorePlugin: Pass context object to lambda connections
94e98281e97cb3150f3e121228b2de2256dd5aff ScxmlEditor: Pass context object to lambda connections
a499058a6f2056e37f8e2c6eb61eae2095bdcd99 Utils: Add push button with menu and default click action
69ce2a40477a912e16dd57bfaab34b850421bbbb Editor: ensure something is selected after model update
0313470db0d5333ea5eda2da81e15c682f3ab981 VcsBase: Pass context object to lambda connections
0d74be319af9425d6fba12a2f06cd3bc1c6ef28f Android: Pass context object to lambda connections
9afcf870a1f6f4d498621dc3fb0c5bb94e03d773 Utils: Pass context object to lambda connections
166f710e621e65c49dd79751ad5083183e3c5459 ProjectExplorer: Simplify scanning for wizard.json files
89453faffe6f08375b9f070fa31f6497fca7e6d9 ClangFormat: Fix qbs build
ecf83d667cf4d5c5ea118af508a667d2f4ffe3f0 AutoTestUnitTests: Remove TestStringTable scenario
4b6c1c9f14a8088e1550a5051adba8f2b92d0d5d Doc: Add missing views to overview page
9a8d34ecf8a5bc39a555f633cb5b8a878a77d3d3 CplusPlus: Pass FilePath to Preprocessor::run()
89f438196454fc070d9cf3270579a2a7d4533966 StringTable: Switch off debug printouts by default
c5a27770f4426038f8a329fca457db8d3a0da5d1 InfoBar: Always order buttons in the same order
8c8f3e30b56175967c5fb91b0cde89c7f66dc75c LanguageClient: Fix handling of defective language server
b7e70592d6e0c8e4c97278209c6f47cec4f1c67c Merge "Merge remote-tracking branch 'origin/9.0'"
bd716a16bbe29aa82ac4e1fd1cee893a588db3d0 languageclient: Write errors to log file
d01173313174765aeb02d4e87447ead47902a8de Core: never disable the replacement edit of search results
b4fed03c27af94bfea812f2ca20e62749d5d8da1 Merge remote-tracking branch 'origin/9.0'
36ffacabe7b6c105f037415b858fc6942624f5e0 ExtensionSystem: Fix some clazy warnings
0b9711b9c50494dda6627e2de894c42223229c9d ClangFormat: Get rid of ui files
e58c60ec9da2faab5a542bd680694c57c6247a5a ClangFormat: Remove without margins layouting from script
a5fb9fc95f8399365d1002d1114d7521a32bb68e QmlDesigner: Add "Apply as light probe" option to material browser
76568d82328b1b2acfb3db7cb21679cce3d9b665 ClangFormat: Create new generateclangformatchecks file
89b81b6046d1a78694a71fa3fbfcaffef17d7784 Docker: Don't show auto detect controls
ebe708c1a3dacf43d3d1daa9ce5cb24e2ea9758d AbstractProcessStep: Remove processStarted() virtual method
c9f7d893f4e9c1c89fafc133b14a38f6958bf212 Merge remote-tracking branch 'origin/9.0' into qds/dev
216f3dd243cef16fdb2c088d51e067968dd3564a AdvancedDockingSystem: Add context objects into connections
8a7b1d7a0045087f0361549fbf73873239b4ea6b AdvancedDockingSystem: Add const reference into range-for loops
c7eb6b8ae6e61132f5bfe0e9c5f136ef5c2dbb15 AdvancedDockingSystem: Fix dropWidget()
fb24f791b85374d41f8cb72cf246aa0243c433c7 QmlDesigner: Fix texture source path for newly created textures
7cf6d9c97bbf501dd3949b30cee57b53ed5abc6a CMakeBuildStep: Get rid of m_waiting field
6d6343061110a1a9d99b6a4cb99c9710d04e6d19 Android: FilePath-ify RunnerWorker
fe294fb00de5507ca1022e3c742ccbfa75da55a1 Android: Remove extra margin from AndroidDeployQtStep widgets
7bb63f5b2194753e78cf8a80e7ab0e04bc921b4d Clangd: Change the Log target to "Ui"
46eccf229c368602c4b6178601a09b8cf7651552 Docker: Add clangd executable option
79c298422241c2b38be2dd5f25df65139835aff2 Docker: Make hostname valid for Urls
0f4430ca05081b12c7b2cfd2d2ff9336ba30629c Docker: Don't try to mount empty folder
7a02b39f265f743879832a8ea5ad48767166cb31 Utils: Promote CppEditor::StringTable
9d5d350f8f216bdce402b4d5b6eaa1f11005c795 StringAspect: Fix setting readOnly
5c7d842975416bc1eaed649a62fcdf7fb7bdbffe Utils: Compile fix after SiC change in Qt Core
b009eff30110397627e4b305ebd43165ea17fae6 TaskProgress: Add setHalfLifeTimePerTask()
0e0a196b7f194447e7b28daa2b8ab086f30bc7ec Doc: Add documentation for new particle components
aa8b9572b40f62b814055b7c339f19a888fd5ada Doc: Fix typo
69c042b6fdbb74cbb8b99e5aa7965f7213922ca3 Doc: Add content library, texture view docs
2987f5a96c880cc708192fc76c77e4101665df12 QmlDesigner: Show content library material + button only on hover
e1fce66f38332eb0995b2a4847429e8a71808c9c QmlDesigner: Remove unnecessary function
88728a414c823dcd18c5f90866b06d857f0ea96b ProjectExplorer: Fix builtin header path query
55284ba124077b64d92d55d9db07a8d2a973f573 CMakeBuildStep: Don't use stdError
a86cb745261d96ce71da306b0b373db726876961 QmlDesigner: QtObject is in QML now
51b6efb1163615c3a5a5f876388dcb8e99e6c8c2 AbstractProcessStep: Don't read std channels on done
dfd079f0503c7b6dc3defd48ed732c3ddb3af9c9 AbstractProcessStep: Merge virtual functions
d8bd889eee25349b1a72da1379d6cb3e91503786 Boot2Qt: Pass remote only path component of remote application
728605b2ae903f786dd26346635d368fddef0c69 Refactor: Extract the code for adding a texture
16ec1ab67fb5728e56aa0a57ca7fb478a0de7b89 QmlDesigner: Fix zoom speed for windows
06b40d0aaad931b580c70d6288facd3ba9f73cb0 QmlDesigner: Bump version in application template
e774aa238be546812754d7ab5497f8603fe51f62 Add "Add Texture" and "Add Light Probe" actions to Assets Library
03525e7d3c4dd36d6ff6f5c60e57d8f69e7af543 Boot2Qt: Feed remote-qualified executable into ExecutableAspect
36c96a0972a4c923e3c6f8045836c336466fee02 QmlDesigner: Use Model ConnectionMetaInfo method
57448021b8475f622c655c0e4dde1d0a3c58329c QmlDesigner: Fix content library visibility logic
44cfc4a8cda8a39a83ebb4e0a7ea0b86b74ef8b0 QmlDesigner: Add nullptr checks in ActionEditor
d9054d1f10cffb3ed957204287add76df58a78d3 QmlDesigner: Add qt insight infrastructure
6c676636ba0179a41a96142937a4d7c9f9aa752a WebAssembly: Cleanup FilePath-related includes
740a65571fd877fb5722443c8172cfdc047aa159 StudioWelcome: Use QQuickWindow on macOS
9e9cfeeeac3c55d8141ce33cee448723c5c1fca1 StudioWelcome: Fix warning
8d6d51ec3eac9e80430588e148eb9fae3c1159cd tst_TaskTree: Increase the timeout
17c09c1e92e51e9831902878d1a94807da88fb4f QmlDesigner: Fix warnings
fd4d1f08fa18327c94fd8046981a865610aed399 QmlDesigner: Add a tooltip to material browser textures
ad90dd958a4e8356f259bd1bb6e07ecaee4676c4 WebAssembly: Remove file access from device
e4c49b720e36b23e9b3e919d33321c84975cf596 Utils: Consolidate the isRelative/isAbsolute implementations
a09e87b3d341c171e35c1b053256cd450160c086 CPlusPlus: Do not try to qualify the names of template parameters
7c0bc1384c3c2209e6e13eb1e26d4d3dbf07ed9d CppEditor: Prevent showing unwanted function template parameters
524f9e063a5df743817a4573f939d42212fb71a8 CppEditor: Make "Move Definition" available for member templates
4e7adc24628a75312541516d18a11ed9cda924d3 CppEditor: Another name minimalization fix
5f7d36d1cbd80444cf00907e2f64b535747ea943 QmlDesigner: Fix material browser's textures search
5f4a5802cc0c9c316b5e816501b356686a47e32f Core: Make UtilsJsExtension::relativeFilePath() work on remote
ad072db2b65e79806c56db794399f6683ef0c9e9 AutoTest: Improve run and debug under cursor
916368ede424c90e9ecce96fe1e5d92690a71fe2 QmlDesigner: initialize unique pointer
b9e7fd2415897b9a17039e747a053e088d22c75f QmlDesigner: Take puppet version into account
072bdb22fc5b3e9c0f61d84175930a361f1cf8fd QmlDesigner: Update isEmpty state upon content library load
714c3c381e16e6763e3984f6e66b274b75ecb42b QmlDesigner: Show content library under enterprise license only
39ffdb416ff3381515b7b4bf579acb23941cd6fc CPlusPlus: Use FilePath for resolved include paths
7e03b29bc7d6f3eba4988e7e2caf9af62f68321b QmlDesigner: Add new menu for export actions
bb5c6a846dba84ce85b00d76e7b6f305500bb9fd QmlDesigner: Add validId verification
af8075cbb20a2c8250b3d037fbc9bc344dc47b9c Doc: Fix missing lights images
dc3a4f0002b2c8e4c437b2d6b3bf9e2e4be91a19 QMakeStep: Remove unneeded override
82ecd3910b10d5722f7c2792e70eb4f96d881955 QmlProjectManager: Use a PathChooser for the fallback qmlscene
c7f9d9af24af1b226e49b8798741c815bc33e6db QmlDesigner: adjust caller code to new binary name QQEffectMaker -> qqem
7e9d50a2c935d7487a1f01cc39caafa4daa7ef58 Remove a few now-unnecessary cleanPath in conjunction with resolvePath
76804a08397aa36bee0f2448ba981ed2f9c8cc3f Android: Link directly to command line tools download
a038ca5fcb1dce6cf908bb8a4e56b5b0255d8ad5 AdvancedDockingSystem: Add const reference into range-for loops
4eee8a12fb71b5404a47202d7d8fe3cd733cd029 Utils: Clean path in FilePath::resolvePath
8679138f73acf201636e957ba772a4a673778c96 QMake: Fix building of remote projects
5e50b9d604ae8855c50f06a19bd1444509163a9f CMake: Checkbox for showing advanced options by default
d701cd5dbe938000ec82c58898cceff208d08ed7 AdvancedDockingSystem: Add context objects into connections
4080f31d25a69b624a2b7f3fd73294943fa9fa6b AdvancedDockingSystem: Fix dropWidget()
6f7837e5e9f7d9f411dd80c206be0f5c44eaea28 Android: Remove gradle.properties file handling
97319457884be9291eeaa259c2f249d2ad30fa50 PE: Fix compile without tests
0c31cc475bb81fd8719e27ce71954ece04b6cb63 QmlDesigner: Categorize slot actions
65f27c765a49288d144d177e1a43d7aaed4d347e Android: FilePath-ify certificate handling
d743770493878114c5fe986a8c600f88f3a5c81d ProjectExplorer: Code cosmetics
1dbf3ad120eadd8c6a72406de9923290dae9339c TaskTree: Fix some clazy warnings
aa69465ecffac94c9c5781cfe3a0d50e8bb49cf9 QmlDesigner: drag effects from assets view to navigator
aad99ea408e3314b138e3c1436f645753f037c6f QMake: Fix remote paths handed to the file cache
fd829240edd683bac3009d5aa9852e247863b56f Utils: Fix addCommandLineWithAnd for Windows
7bc86a8b8d1be656d674dea7e2d1cf502c92163e Utils: Fix clazy "Mixing iterators with const_iterators" warnings
5c03387c13eb25b8e2d556ec87297466976b6f8f QMake: Remove some dead code
ccb2317a16388eb3026877294bf1350834d2737e Android: Fix clazy "Mixing iterators with const_iterators" warnings
4417c48e7b8f9564bb780b14b6ddfb9a209b7c87 Utils: Remove FilePath::operator+()
5d9cb68a36d0e82f33e5426dd2ec9d9f39035516 qml2puppet: fix include when building against linux Qt5
036948c6286cc01d8e3bbf0e594dc0bc5358e43b qml2puppet: remove unnecessary flexibility
b183efc94ac0e5df2bc1c9892eedde5e499c415e ProjectExplorer: Append paths, not strings
3d158d73ca64c14cd84f29a57e996d3d5ae23ced QMake: Constify some accessors
8fdbbccf3fda998c145a3ebd03cc29c5efd79881 SubChannelProvider: Pass PortsGatherer
d29fa0f3babc402e2fe56cca496783956aef08df TaskTree test: Detect test timeout
b5befc694514d1b223b56a5c65fe4ac859cdbb2f QmlDesigner: Fix content library sections not collapsing after search
d1c7fbcb1ade1bc1067c5bc44367cfa254c3cbe4 Core: Remove QString overload for GeneratedFile
be97b3595915ebb03f1187daae0b011b1bece718 QmlDesigner: Add tool tips for animations
19f1c6c32fb5af0969da0a9f94adc277edec6af5 QmlDesigner: Add tool tips for positioners
302268c4d4431d7311500419347163fc78d5a904 ProjectExplorer: Remove ChannelForwarder
ac966d7bb9829e7bb2ec9e9dd59fcf68fd3b23f9 IDevice: Provide default implementation for signalOperation()
ac760f8e4ac4efe1731f4c76a6dd624a8c237ab6 Core: Properly consider text codec when rewriting header files
9dc5a9ec00ee361f936ae26bc3b226d47f10bf9c CPlusPlus: Code cosmetics
cdbdde7d37d0b34b5ac34c22a7acbf5c1e8fac9c QmlDesigner: crash fix on shutdown
bb34f507dca677eef1169058144737e7f6936521 ChannelForwarder: Remove unused private fields
ef6020ae0f9bd420cc99386a5163c2334edca7af QtcProcess: Set Starting state a bit later
b31249d753f6845d8390a5db224ca7a80b99da4e CrashHandlerDialog: Use Layouting
83720540a1370ef455cf129beb3c30fe565e0b68 CppEditor: Convert parts of ModelManagerInterface to FilePath
9a32aae70678ad88a04174e0939be17326aaff11 ChannelForwarder: Remove unused m_fromUrlGetter field
34b966c21d89f262f9cb1eae6c3265e19bd0feec QmlDesigner: Show informative message when texture library is not found
26c1747ae658a251bc9c6dd6784a7c8c0037a1ea QmlDesigner: Fix issues with adding texture as light probe
86ed12e7299c1e6be61f6c42c486acb6fcd4c0de QmlDesigner: Add StateGroups to Connections
8249eabbadba2acdda5bebfd31f2be56a3c1928b Utils: FsEngine micro-optimizations
f7d022009ba4ef00c93d6cb159edd4c058216e45 QMake: Use FilePath for sysroot
669242b9d8f0788cd44062c30cd081f8781acf50 QMakeProjectManager: Progress towards reading remote .pro files
3537c9974ebe2d7cd34908d2fdfd143bc985a238 Utils: Use .path() instead of .toString() in some local cases
50f6afe4d0a8d152bcab7b5fa3c7b7583a9cfa3c QmlProjectManager: Use a PathChooser for the fallback qmlscene
9a9061a04ab9d50291f9b76f7db856c96b82178b QmlDesigner: Fix warning about unused parameters
03a91f9f98e0b3524be1be4e9c54c7be75a6b855 CppEditor: Remove Snapshot::remove(QString) overload
3694d00dce77576ff60fe8ce31d8f7fd6e546c5d Optionally use qmlformat for Qt Quick auto format on save
a157de7877396b0580d6133d866b3e722844ba7e Utils: Fix expansion of macros in FilePath
0bd6d7a69fab1cc622f6e00ee63a15c666967e0d LanguageClient: move completion rerequest logic
8ff969d14995d1b81b1704bf203f142f50c5a9e7 Core: optimize search result item text generation
5b6bf2914382696176ad664b7548dee1fd44f239 CMake: Add a preset for a minimal CMake configuration
5705194bc78709fbdfc125debcfd228d20179e74 QmlDesigner: Fix build for tests
397e5f302247508806f0c8d23f63b05b6ac0aa18 QmlDesigner: Prevent content lib scroll when context-menu is open
157f2bcd3929ea07cdd14c3930cef2313fc470ae Merge remote-tracking branch 'origin/9.0'
8e5c84e3ddc77d049904aabba05d8ff6ef098c6b Git: Disable instant blame for modified files
196e73fa163ff463570e9ab0e28ac04b4df9c69a CppEditor: Convert AbstractEditorSupport interface to FilePath
f68db427ef763b995f9e4fa3f1dbf2c9b90ad943 CPlusPlus: Migrate Macro to FilePath
3c8c5b08a871018c20140b126834af964139a2e6 Android: Simplify 32bit detection logic
aed633712fdd85aa4f7a72794f976eb5603d3821 CppEditor: Prevent /TC and /TP with Objective-C files
5b19c034ed28053e52988115464dd1897262955b QmlDesigner: Remove error states editor
4b72ac3022ecf636c3feeaa596b9bbeaf4cc97d0 QmlDesigner: Fix states editor dialog not closing
84104499b74f54f3f86f27f8d872aaba0d461112 QmlDesigner: Add missing ItemLibraryItem property
a1464035962dfcf871773c28216f79ad228b26a0 Utils: Make FilePath::refersToExe(...) return the found item
9e0ed9eeac9adf3948d7640074f2cce504149cad QMake: Remove one occurrence of QDir::cleanPath
12b639970fc3ce08eada69d315470735fc23ef5f MathUtils: Add exponential interpolation
ee10ba1909bec243e2a65d5fd191e38628b1d130 QmlDesigner: Add support for tooltip in item library
5544cdc276612de1c1672fc89a808e4f471f713f QmlDesigner: Don't create duplicate default texture instance
23f53dcbda305d3e2f593b4e0c566375af7dc0fb MathUtils: Add tangential interpolation
6f299f19ac7fab734894ca49395d19eba7c36681 Utils: Move DiffUtils::interpolate into MathUtils
7d13c827503426bbe3519a0c8d7ac302be373834 QmlDesigner: Expose realValue from SpinBox
62d86837e9a20db22b425fb1efc3b3e1c76cefe8 QmlDesigner: Fix states editor none visual items
ea55e010511c1032ac8dfad411403e4b0f4211ff QmlDesigner: Focus material browser's sections separately
ef6bf3cb52b8fa5e95297e9b531f39c27dc4e9fb UpdateInfo: Merge "new Qt" and "updates" available
d15c102af89b54987d8f7a7072cf8e9099fc0361 InfoBar: Do not synchronously delete widgets in update
ddecd338a8d0c7a446b23fc4076570734131a8cd QmlDesigner: Unify texture image providers
3a899b34c7fa6ae22708fbae2ebfe39c6c3e33fc QmlDesigner: Skip imported files that already exist and are the same
674ad1ead16dafbef53555dd4ff8e32b57265523 QmlDesigner: Fix compiler warnings
c071547aa54b519567b6360af327748787e36141 qmljs: fix warning
907a64ac324f5038ffe809891edbdf3b3b5fe32f Fix bug: drag state remain on after dragging asset to texture
133099aa81c3d3ba0f7ec8f3490cbb3942b66966 AbstractRemoteLinuxDeployService: Simplify internals
49d6456b66146d5f58a63ff5031d58d9b82d173c CppEditor: Remove WorkingCopy::get(QString) overload
9ceaf399299512f36a4fda37826bc6e21fbd26e1 CppEditor: Remove WorkingCopy::{revision,source}(QString) overloads
f2f1b7c2d73000ce1cafd188e184d8b081117459 AbstractRemoteLinuxDeployService: Refactor API
bec3b9279b8b4cba1e1f25269d9bf7edf2b14d03 CustomCommandDeployService: Reuse TaskTree
c01ab460c85a4282376946bc511b4580cadb3c97 QdbMakeDefaultAppService: Reuse TaskTree
9e810acfbe9ea1923dcd5530b530d1d79ee3513a QdbStopApplicationService: Reuse TaskTree
2853d390655f1190d68f87208b76991e4ac0fa3e KillAppService: Reuse TaskTree
0c18fbc31f8f0cad3c6375dcb59911b8fb2af025 ProjectExplorer: Provide task tree adapter for DeviceProcessKiller
c99dd6650d5e511869b8bf75ec1ea1ab43aed1ce UpdateInfoPlugin: Readd vanished comment
c38a00513ef1d1733e0dc1b34d4a44cd20392372 ProjectExplorer: Introduce DeviceProcessKiller
d4871731bd1b2c623c7982c2496b0b3913112f71 UpdateInfoPlugin: Reuse TaskProgress
7afe536a5c7de0e2a9d60660b7c34088b8c64514 AdvancedDockingSystem: WorkspaceDialog: Use Layouting
bcdfe1cedc3557709c4d033bcae31645019d2f89 TaskProgress: Add missing features needed by UpdateInfoPlugin
d90b1946d49e2d477b5d6390a7fb24cb0589e49b TaskProgress: Improve fluency of progress
16f671874fde2f9d6c1a674a0af8a56b4691a82f BuildStep: Remove unused runInThread() and cancelChecker()
1e632d0c25bdb0d29361e88bb7dcb22567e2b047 AndroidDeployQtStep: Make it cancelable
21c659400a6ecbf6b49986e4b46d02b914a0ea26 StudioPlugin: Use custom openOpenProjectDialog() to only allow qmlproject
caccbe8377400ba2b6e3a272a59a4d7174fd594c QmlDesigner: Rename setting to avoid any interference
056165d0719c275479358bf1c7db89d74906de6b Use double underscores instead of single ones in QML
afc7cd2c98a673d843329dea38da271865c41ed3 QmlDesigner: Use a vertical scrollbar for "Confirm Delete Files"
038771051d62824368ae94b73e07c1b5ae62cec4 CppEditor: Switch to FilePath in IndexItem
6747e666b9cc9c47b26b3c2971e56b83ac29c7c3 QmlDesigner: Open TextureEditor by double-clicking a texture
45c7a6b8e60955d53601b19b6aeda51c5310db4d Implement drag-and-drop from Assets View to Texture Editor
dbdabf9c27eff015d279a1198d703178bfc0fece Use M_PI for 3.14
0a925009a1c9cdc722dbc578749da303d5d22b34 RemoteLinux: Fix const correctness
4ad9f539890d78a40e38cad61b2cbe364abcc15e Core: Rename DocumentModel::fileName() to filePath()
40ba25b69120def09ff6c25cda77fcb0a377417d CPlusPlus: Return FilePaths from Document::includedFile
ef9777412269f5b5cde862d133816373ef1ee57d Core: Rename ExternalTool::fileName() to filePath()
67e9c2d7a0902154facd2b4dfaac2a745abded2a CppEditor: Proliferate FilePath use
3321261cf3f2142017b80d1710027c2940fa19fd QmlDesigner: Fix DragHandler being disabled
74ba41f82cae715604fbedd8cdb156b9bb7e6102 Qnx: Reuse setExtraTasks()
fbe8d854015887dbc27fa6189749da678ec59289 Qnx: Reuse setExtraCommandsToTest()
6dc1a1e98f02ec057a4d5673e8daa165e5eafa92 LinuxDeviceTester: Avoid non-trivial global variables
b34fc8a4209747ef00a3e30a447aefc01b9af2af LinuxDeviceTester: Make it possible to add extra tasks
dd2c4cc6ea97ab8cac3d56c38b86d8ca315257b5 QmlDesigner: Fix cloning of extended states
fd1864d6788c3a9ece3aa549316c7af8600de15e QmlDesigner: Fix disappearing plus states editor
658c9f6f06a421bf90c357cf1bd5e3b0c8a51b1e LinuxDeviceTester: Make it possible to test extra commands
ccbda4655e8ab8b39b9f6beaa53f628c33e06af6 QmlDesigner: Fix wrong initial render type quality
2388caa5440b5c87e39ee86c2921c0340e833dd6 QmlDesigner: Fix wrong state group shown
7582b62ddfdf1f1834143b587ef45b585990073d TextEditor: Remove unused findFallbackDefinitionsLocation()
7fd3a5e66e2b9e0f9b852f1ebfa942e31ab69e0d CMake: Use FilePath::suffix() instead of manual construction
c10600134f93ec84bf825197b8d5dbf0ec6af359 Editor: do not paint rounded corners in editor overlay
02c65495d142a96506c6257e92c403386f310df3 Merge remote-tracking branch 'origin/9.0' into qds/dev
3d05611726686e826ddeaf435d2c61628576cdad ModelingLib: Replace foreach with range-based for loops
a94f37c8b06350cba94c3a4b6221f6df0a8398b4 Utils: Add FilePath::{suffix,fileName}View functions
46cfdbf285aedf6cbeab80f0475901fc8309acad Slog2InfoRunner: Reuse TaskTree
6fb0bebba9b362e7ed8f008de6d37f9799e377e1 AbstractRemoteLinuxDeployService: Simplify internal data
d2408fd3894cead458263860036004fa147525b9 Tests: Replace foreach with range-based for loops
5bb46fc998479ac533dc996db8c8c3fe5634114c MakefileParser: Replace foreach with range-based for loops
68116ae4f1cd4e1f0c27c6e8b708c79867ee0028 GLSLEditor: Replace foreach with range-based for loops
6a20c52cc9cfb8850dae4e3c868a5445c00684b9 QtSupport: Replace foreach with range-based for loops
849c902971642d25908ea27dd79972c0ce585003 Qnx: Replace foreach with range-based for loops
9d80e23256ea139e4ae90187bd8872e6a1fb3a75 CppEditor: Proliferate FilePath use
8d645a506de17b989259fd90ea4e382e06fbeab6 ProjectExplorer: FilePathify extracompiler.cpp
decbb93069d41dba0334345729c94c8a0f8db78c MsvcToolChain: reduce severity of task for failed vcvars runs
09c4df833babd900544ece35328c232ec33858e2 QmlDesigner: Fix small typo in MaterialBrowser
39d0a554b88e41375b771c042aadb64bd9c67d4f QmlDesigner: Fix initial positioning when dragging item to 2D view
fea463bb4a93f38c01dcf9d81abcdd8a892b72be QmlDesigner: Drag textures from content library to material browser
4536258c9edc14906d7c6f68e7342ca55578b0be Android: Revive second half of AndroidManager::updateGradleProperties
34b236e7fb3ea4f10221b7693f02df59f774e3f5 QmlDesigner: Disable assigning a texture to a model with no materials
f2d50ba6ffe9b799e12b3c2adc9d17872315b077 GenericDirectUploadService: Reuse TaskTree
a6015a62160913e3477244b38482354b3cdb4224 LinuxDeviceTester: Use Storage for internal inter-process data
07e96c299a514628c541ccfd8957a8b14a07b27a QmlDesigner: Allow to disable possible imports
bfecefabc05717c29f7b4f572314fabf4d031796 CppEditor: Let users check for unused functions in (sub-)projects
110332e920439a7ec41ba39b34e8700554d7208b GitHub: Build with Qt Creator 9 and Qt 6.4.1
43b21595e97d0260f811389db9db8d0d9a4d0a41 Utils: Disable broken commandline tests on windows
6943322b4292f89a472bbd920251b0add723e27a QmlDesigner: Suppress some unused params warnings
a3753deebeb881806686721967437790d9169bb6 Fix build error
b841d68a956fdc987ea60cfab19f872cccce4a7b Wizards: Let "new file" wizard create subdirectories
953051b8bba0b0dce4a3879e5f2365e6f82e47e3 UpdateInfoPlugin: Reuse TaskTree
5e5d412fc4f35e66b83f40e8746d7cbb1d8c33cc GccToolChain: Create process on stack
706b0ff9eba7d40a3f9c1fac1a5f9784611b9461 "New Project" wizard: Create project parent directory, if needed
2e75492257d48d269c5b00856f4591d4ae5b8726 TextEditor: Replace qAsConst() with std::as_const()
3f5259dd00a3d1cfd14c6253955e74b44d76c61e QmlDesigner: Fix crash
d0a07dcacb70eaa58b36cd9fc3b37f8c8ce8106c QmlJS: std::set instead of QList
f9b4bcd3d83825eb5a41713d1e04fe7fa30c21ea TarPackageDeployService: Reuse TaskTree
c88a82905921565053a43beec0ca50c4e0d7f583 RsyncDeployService: Reuse TaskTree
587bafc771f49e8af545e2e08632a59fd9a1db9d Utils: Make errors from CommandLine test better readable
85340d950898f95054a3a36291a28a859e752b9f TarPackageDeployService: Get rid of killer process
dfd06ec1756b1a99ca887d5597c8f26d05d32ddf TaskTree: Fix destruction of running task tree
3468cd20ca8feeccf023091582d1390947822bd8 TaskTree: Introduce Storage item
1667b062362f844d2542a4af2d47c40c63584e78 TaskTree: Introduce TreeStorage class
129bd24131f2a240a60ce67ee0f31387d8df2da5 TarPackageDeployService: Don't disconnect from installer signals
2a830a12f2bb08517d04f1ecaa249b8e848044e7 TarPackageDeployService: Remove removePackageFile arg
adc064d5022d673898db9f0c26aaca8b675c0ca7 Merge remote-tracking branch 'origin/9.0'
910a8864dc43f8dede7617855282f19de62ee939 Use QML TreeView in Assets Library
918c7ca52ddbb446cb4af9bac1ee017fed85b738 ClangCodeModel: Make use of new clangd token type "modifier"
a575cb4f46b8f24ef0a5cc15702a15020016d343 QmlDesigner: Implement drag-n-drop a texture to material editor
7e9cab6e788aabc39aa1f5cc5ec8d1ca2a6a81f3 Utils: Introduce a FilePath::fromPathPart()
53526e4d4bee772e98ce48b83c37bcfd53cf7241 FilePath: Backport some pathView() changes
c7c6ddb8b07251270b42f648ccd5e08c6e8b2127 Utils: Replace Environment::isSameExecutable()
bf325fd6af47a72764fc5df154621ec3e6c713c4 ClearCase: Use a FilePath for the test file
a2baeea3941a753b887162376937790f70def3fd Git: Enable instant blame only if current file is managed
fa1adf4d4001207902a5572b39da4f1cbc8752f1 CPlusPlus: Proliferate FilePath use
822e2a224a283581b38948d4626f873c6b38c044 KillAppService: Some cleanup
8dc13379dd584872c74fd550245a3840a03cab5c KillAppService: Use queued connection for signal operation
0ced4ab061cd459eff82b0d7159a452c35383a38 Utils: Remove unused ProxyCredentialsDialog
fabf2f8cdeb5d0b6b53fa9be497057a0477b41c8 Android: Rename Android Settings names
75b43de14a64773bfa9852a79698f3893b143282 Docker: Cleanup docker process interface
3e6c3d9fe7328e824fab14a201c6560cb8bda94f Utils: Add "addCommandLine..." functions
83fa44afd4588f0539d5a25f3205a732883b6d3d qml2puppet: qt5 puppet fixes
b1df55426a23b60da79316090ea07c8f1b56cc10 CppFileSettingsPage: Use Layouting
c960d75b33a55f42431590c81fdf85fa960a1756 QmlDesigner: Update Property binding instructions
d2b887cfadaadcc116f20389e09beb11f4abee01 RsyncDeployService: Remove setFinished()
0312bfc06609d491f4eb96a7fb79d8ec6fb15811 KillAppService: Don't call cleanup() from d'tor
1aa2d8ad300fc912d14f23988542effbce0e0a2b QmlJSEditor: do not wait for the semantic info update
ac1af9a58211e6efc2b265c79ff192dc03e16ec2 Implement texture context menu
a130a7ae0bcdd7cdb415be92e0c3a2b95ff94ac5 Android: Remove support old cmdline tools
e3a817ec77f653100bf29389464af8ac8706d8e3 QmlDesigner: Hide or disable material browser and editor if no library
e8454dc5fedb5922c4ada6d1222c27b244eef3b7 QdbStopApplicationService: Always call stopDeployment() when done
94cc91b8336274046e00e4347f2febd2c5e905af QdbStopApplicationService: Move setting connections into c'tor
197002ef048a25ad5b2f4407bff8fd38b1329581 QdbStopApplicationService: Remove cleanup()
20aa5d4888e25e416a5259202b87ce34ecb72adb AbstractRemoteLinuxDeployService: Remove setFinished()
f70cdf52c4d58051bb1c65eb01779a63e2dc003b QmlDesigner: Fix created effect cannot be used
6d620429c6e1438c9b479479570a409fc65a0ca5 QmlDesigner: Fix Material/Texture Editors toolbars issues
3e941652e14c17e8e154a57d77dd0ae469706daf Utils: Reduce scope of Environment::appendExeExtensions()
a333efe901c0c737c81d1a4426f040b2e2ce7413 Utils: Introduce a FilePath::refersToExecutableFile
9635b1545b8f0946b0206f96c24ce52ab4e881c0 CMakeManager: Avoid one use of Environment::appendExeExtensions()
3a7fee0cc659393694cce125dcbb2b64c179b1bb Git: Fix crash when closing last editor
252179b9383bb98ebfcaf89d0dc3d19a063d7669 Git: Stop blame attempts for unmanaged files in the repository
4d63f2a5983c75b43a145efaa1a9b6787972c432 Git: use unique_ptr for instant blame marks
a04f1590de94405d1ddb8543d0c2e58f28bc8127 QmlDesigner: Update texture preview when source change
4b6b81cd76ec341aa5a83d3284ab5c71f84f16be QmlDesigner: Fix content library material unimport
88ce27736fa6324aeeebd30bc79a6d7ffe097d82 QmlDesigner: Add separate + buttons for material browser sections
722312f62fbc5476b1938b4c98df6fa86c986257 Core: Combine two code paths creating absolute paths
ba80985769c550f31995f0c5254cf748b941dd91 Python: Replace one use of QDirCurrentPath()
e567cb313bde692f888ab4968fbd784a42f3a761 Core: Use FilePath for working dir in MainWindow::openFiles()
fbc2da621caba3984ec6431e75da39a5b486ae14 ProjectExplorer: Postpone search of custom executables
9e48f63a07669b383da1a05f5e9563aa1baa3afc QmlDesigner: Remove one unused variable warning from ItemsLibrary
33ea6f24333805381db63c4074315748bc3cf58e Utils: Don't resolve relative paths in ProcessArgs::prepareCommand
fd177245976f01b4d61e27759fb51bb0ea8eaf2d LanguageClient: use reported rename placeholder
02c041c13a747d2cf42866b441715d2858ecaa66 Utils: Introduce FilePath::isSameExecutable()
b6c2b08555f5ab883601ddb0ca5aa41d067f0269 QmlDesigner: Change icons to original ones for effects
ab7b8052961a66d698af43cbc1a4083fdc2c31a1 QmlDesigner: Update Qt Installation instruction
815dd39e478378b01c3489d9ecd2e85dedb75b48 QmlDesigner: Implement Texture Editor view
0f2db176fafe6b981f4fb8d35d7d7070dad3d92a ClangCodeModel: Re-enable renaming via clangd
04116b89df0e69780046ec1e4ee4fbea493bccfb Merge "Merge remote-tracking branch 'origin/9.0'"
ea79027e20e1ec77d7d3aa26cc5b3035889477c7 Merge remote-tracking branch 'origin/9.0'
ccc7e14246d8b9bb28c4dc6d5ee31c52b7ff97a5 DeviceUsedPortsGatherer: Fix endless loop
cae1936da3ed1c3f1cd8beda138c9fd7fd20d2de Git: Add instant line annotation (blame)
ea917a0aa677f807bb7d88f006f59432899cef3e ProjectExplorer: Improve UI for project-related closing actions
292e8f510e0b302377e6df43c39d7a214035b072 Add manual test for TaskTree
34a7ada66b7c0485bb6143ec174a55257f38306b AsyncTask: Add async task adapter for task tree
b4142684276e4fc67f13c9b3eacf398a1eb6b6b6 DiffEditor: Use AsyncTask
30eac65e09028c2b7bf0b2f695248715b83c5fd7 Utils: Introduce AsyncTask
95609cdd576797f07c0b92032271069778515f9f Reuse TaskTree inside LinuxDeviceTester
3a1f94ec287934afa02af2dc8031b45a90110039 Add adapters for FileTransfer and DeviceUsedPortsGatherer
5d4667e682a1d90da41a371e1ffb4a52c6bb890f Introduce TaskProgress
4e4176a3d3003ca20a98c5d64ff486c6f8bf6f1a TaskTree: Add task tree adapter for task tree
d21acbd413c486cbba11bdb906d1e1b0b0cce416 LayoutBuilder: Add support for tab widget
1f59f42287b92c022aaace92907c7442c1a01b45 Remove unused includes of QLayout
02f384115e615b836d70c284095153d89738fc8e Remove unused includes of QBoxLayout
bf0d716b51c9eb78076f402e60e68acd4858ee7f Remove unused includes of QGridLayout
3caf82f746a10ab58b94ac383a805ca7813edf88 Remove unused includes of Q[H/V]BoxLayout
5688c84fcfbf9f73df614e369558333f216f5697 Python: Initialize settings in the pimpl
5c37cfc54bf7c40f0b28e11a56622732dbc921b9 FilePath: Avoid QStringView::first, which is only available in Qt 6
661bec83d46d8cbde83fee9608a09021ff3e56ec Prefer ARM/X86_64 for Android, prefer 64bit
9880d9a33a0ae23d5262a4498b38a4517d2ae34b QmlDesigner: Less delay in opening context menu of 3D editor
1f7383096c74d09cba342e2fc3de9e2f42a6200b QmlDesigner: Add more output to debug view
c84d12227d49d84922448cbd217efd920af6d8dd FilePath: Fix pathView() and use at more places
db76087213d516da66d61a1e9aa8798f1f78d8d6 Git: GerritPushDialog: Use Layouting
e47141753995f91565f8e3781315b40fe620d16c FilePath: optimize comparison
6046fa8354cb0381b972c6bf91d23acb3bf29404 Utils: Fix FilePath::hash() regarding to case sensitivity
47b5613c532f9f38f3d79e5389d358a734a59828 CppCodeStyleSettingsPage: Use Layouting
1a70403f9450beaf5c31f7fc0fd90778770e231c Remove unused includes of QFormLayout
a2f09223343706e934a0f4816e39c22274be45f0 CppEditor: Inline cppquickfixsettingswidget.ui
cd68ffeac9c2e3ac477b3a164a6ea6e81ea550e0 Merge "Merge remote-tracking branch 'origin/9.0'"
81629e92287fb1101ac65c3d069d019190204bcf Merge remote-tracking branch 'origin/9.0'
0a2213d3594ce289f762c08ffd05caae818ceb58 TaskTree: Add default c'tor and setupRoot() method
7ab95906b96e37eee6bc60ad8ee9094f7eedaab4 TaskItem: Move enums outside of TaskItem
a4b7e10861f0f76f51379fc20e79f76e7c44bc05 TaskTree: Implement simple progress reporting
e38db0620370b4a35ec156fd9e5b1c596a1e4335 TaskTree: Add taskCount() getter
234b3d1e6fb243766a70589139bc481661297ea3 TaskTree: Add DynamicSetup handler for groups
9e8d208cadb004abd7a71b6362ac72e7517bff5a CMakeProcess: Do some cleanup
165d364a6d7a6c7fa352f529b782eb357d76eb67 CMakeProcess: Simplify implementation by using ProcessProgress
953000b981eba2b98e359c700668835272297fb1 CPlusPlus: Add new usage tag "Template"
409f7f226fd4b5dd99441bf466394d4ecde1441e deployqtHelper_mac.sh: make sdktool optional
e0b856f0b06f1f0b864ee4bcc230247f66122b24 Debugger: Remove DebuggerCommand::arg(FilePath)
04864bd0d1fa26b323c84ae16b25833b1923df4d QmlDesigner: Show message when generating package
c5296c0bf65fdc2742ab52219d484b53a93f549e Perforce: Inline submitpanel.ui
0e4b0a26d34c523463ea68b27caf69cbb89083f2 Editor: move ownership of assist interface to processor
aa6633ca2157b686d5398e8a09f538c15089fd36 Utils: improve assert stack printing on windows
86770f28d0da13facafc5a8c03d84bb331b51382 ClangCodeModel: Keep a ref count for "extra files"
e202138fb0a79f009b8e180f008d57ae4b03de6a Utils: Use QStringView in FilePath implementations when it makes sense
fb4e22ff7cd5286755e61ded62bbea5cd11e465c Docker: Add option to enable flags needed for lldb
e3c2db8db585bfd0bd2be297a808a1e3288634fa lldb: Use path() for FilePaths from devices
56fa49d8e6cd7ac378deee3ec1fc95d041172d01 QmlDesigner: Allow drag-n-drop a texture to the 3D Editor
f6d878f037ebbc7dc011cb7f067841b9db0d47ff QmlDesigner: Update the wrong Timeline option text
d97e9a81b0a979a7c7a67ec99068bd98a13748cb qbs build: Don't build qml2puppet
69461e4c68fee8eac734701b36694cac2acadfd9 QmlDesigner: Generate material browser texture icons using provider
92f7da917ea7ef0a10320251f0556c16872a715a Debugger: FilePath-ify .so libs handling
9f397de829b3b2fcf44ea8a67e3537854a07be5f Utils: Don't reverse FilePath::dirEntries result by default
f59ca97a847c65444a3eeec9f4a7b559bd8cd204 Utils: Sanitize FilePath::onDevice()
96b0cda1c9e5d4420dc422ad46dd6aeeb8e769e6 QmlDesigner: Don't write zero position properties at node creation
213d60ad6427229080d4611a11ed42c94fc8063d Merge remote-tracking branch 'origin/qds/dev'
2a44f8caafbe8d7ed85c136616f9e82ce0973893 QmlDesigner: Fix shaking items when scrubbing the timeline
f6cfe31c7f834bc6769be7d4928d30b7188002e8 qmldesigner: increase restart timer
6296b9a1a415d7e9f3674ba73d09e4b5c72cae2e QmlDesigner: Add option for Always auto-format ui.qml files in Design mode
cbd96ef3125a1417e68c92c20b015a3c285ac33c QmlDesigner: Fix crash
626e1175a60145cedbf177a8df1f4beabfdf45d4 QmlDesigner: Do not specify version if it is -1 -1
7a65a1671852e57f6f88a66a85c2b43fd528cd9a Designer: Assert url scheme of the component
913d0e079a3ffee24c49b11dd52815e45fae710e Designer: Fix missing semicolon in case of dynamic properties
9381429e4fce3fc9a294fee2582266439a3ba6e6 Perforce: PendingChangesDialog: Use Layouting
46183c88d11d2f6226ef2a170fc570edb1d63af3 Perforce: ChangeNumberDialog: Use Layouting
478ce3f4a409093ad29dccd1776b2ba8d02fcb82 QmlDesigner: Fix condition for adding QMLDESIGNER_STATIC_LIBRARY define
dd4c47a73a2a0491532eacea5afe7ca047ecc9e7 QmlDesigner: Avoid warning about unsed parameters
0d2cb70fd6bded27e97b62ceec24902330bc78f0 qml2puppet: fix broken deployqtHelper_mac.sh
28b76e50cd9dd9ecdec5807bc438581012751910 PluginErrorView: Use Layouting
81a4d84194a3e6949ecb37cbe31c36d869637dc7 PluginErrorOverview: Use Layouting
450a67cdda101ebebb9a27fd18020d9155780bfa PluginDetailsView: Use Layouting
301e7398d9c494cc3fc6ca7037fb96572bf2e072 QmlDesigner: Fix ProgressParSpecifics
ae67799eb369da72a3a2da3fff0fd14dbf1a6e39 Merge remote-tracking branch 'origin/9.0'
f297b3f1b54ac2c4326ca1f55bdd0705c626ec35 Editor: always configured assistant
57d3af6d891ea28a041a4f97de33e7624b172b22 QmlDesigner: Fix state group selection
a7cf52cc894a6dd1a3fc19282986ab0e241db19c qmldesigner: cleanup connect
974c28ee776d3cc80db5f1fbec7a9a2dd33446c8 qmldesigner: fix possible crash
be3a781581344d25bfd9381e1c02f9fc5409e4c5 Debugger: Exclude already-running processes on Unstarted App Debug
eae9be421826e78ed567123ef85c2b3cdaba1b1a QmlDesigner: Unify state handling on puppet reset
4c468e79f7b76d0261251f9b754ee5c4c284793d Update 3rdparty json
2bcdc1ee7e999f4dd9cb77872637703111e471c4 QmlJsEditor: Add color preview tooltip
90ee349980e64d0510515d3db087bc36322f325a Android: Use FilePath as adb pull target for the deploystep
3f0f9a63c0177e034c659f23882f309c00967d55 Merge remote-tracking branch 'origin/9.0'
873ca6d3913efadebf32bac4ae6f8d637660c1c1 QmlDesigner: Fix scene environment color for 3D view
889e999f32bb2a0c7759e390455d4cfefc333a98 QmlDesigner: Clear material preview cache on detach
09ee528c40de17401487974dae5ce708079ac8ad Editor: unify assist processor handling
b10c135a1026b3d379ec7cf47907ab08c7dc5ba4 qmldesigner: remove delayedResetView();
8c7ed5a03eb777126f5c4a2d5caf69232b26945c QmlDesigner: Add effect placeholder icon
63d30351a67edaed447f0474f19feb53dda970a2 QmlDesigner: Add "Select Parent" to 3d Context Menu
a43f5db5827a96faa679a00ce643739f86615d1c QmlDesigner: Drop static_cast from connections
1b6fd9f32856234272099764bd7b28a36ccf9337 QmlDesigner: Update SpinBox
ff5e6a154b526fcb5f74986ddb4d63d5348d668d QmlDesigner: Add hasSignalHandlerProperty function
dbca9418ff420279d028e7a143020d0eb022cad8 QmlDesigner: Fix CheckBox spacing
f7e7bcd877f69f3851e244662e424a6d29ebd16f QmlDesigner: Add link error switch in RewriterView
cf86bc789f20b44d20122bb7863819423e0fd0b7 QmlDesigner: Add insight constants
33a33612c8ff92bb541d76cb338675f16beb4281 Theme 'dark': Use accent color on macOS
61c133f48a3e7f0afc8f5f8baba01e3d311709ef QmlDesigner Fix: Select Background Color doesn't clear Environment Color checkbox
1a1d4780b84c669cbec218d6c1e688681893f276 TextEditor: Always use the same 'x' char for width
e3e5524f88a0919ad2491c8ce83d5104bfa9daed QmlDesigner: crash when dragging an effect twice to component in 2D
8bb3c8f9237d05aad6d880c87a6e2382e5e72457 QmlDesigner: Don't cache possible imports until code model has settled
455c84ccbf544120adad8d67ea0a43fa628f6a25 QmlDesigner: Add Duplicate to 3d Context Menu
b8a3f8d277139028b7161614631558ff45ec1a7e LanguageClient: fix compile
5eca1ff8736f37abb51179b893d172038b5f715d LanguageClient: use internal filtering if we got all completions
a2b85953cfc8577fc36ca8a7a12c635a0b9dde6f QmlDesigner: Paste to the clicked position
019e44ce2763dc12b3fecae1c159b48869f15e4d QmlDesigner: Rename Projects to Project
06191a5883a384611b6437f4634bd4c85955332d Adapt to upstream change
63a8218680c6f909208cddfa18d92d734cc40df8 Editor: consider all providers with matching activation chars
1f6511dba0d0460bd2121fd098275c79903d8d4b QmlDesigner: Fix findCommonAncestor()
e70658d9e9b6f90b3819a632cb13042c12dc7343 ProjectExplorer: Use more FilePath in session handling
16dde463ed4a447e11b670106e016fccaf8cebf0 QmlDesigner: Remove "App" from the Components view
cbea3f15cea19cfc01a8f81874ad6d5a3d06903f QmlDesigner: Allow material browser texture selection
f147ef73ebe5165a8e843424b095974c837e4e2f Fix compilation of tasktree tests
10dca6b37f9479396e9907ce53f7affadfbeeb5f Merge remote-tracking branch 'origin/9.0'
bb64505cab8754e114231e8947cad1444db74014 QmlDesigner: Implement adding a bundle texture as light probe
08f87321085da426144de11b68ef0f8153718670 QmlDesigner: Move nodes to a specific position
c8fbc0b13c74b446312111a62a8c27a59318507f Android: Code cosmetics
7fe256ca325923db150f5da2ae87ebda019a2543 QmlDesigner: Insert CubeMapTexture to correct prop in SceneEnvironment
7205ff6c25c845314c503c1a17c7ba0b92fe5529 Tests: Fix compile with Qbs
d81abf1fb46c8f30bfd52f9ca95c2da941266b25 DiffEditorPlugin: Expose plugin's future synchronizer
c49de14c9dd4c92b25642d379d525a057fa0d409 Utils: Introduce TaskTree and Tasking namespace
cc690a407f07f24eec4cfd7b0843f8fbff34ff31 QmlDesigner: Remove macos specific puppet path
7cfad178506c7344e29075e334a57cce6cea02ed QmlDesigner: Fix warning for QQmlListReference
f56420ee07c2381bb4fc1ca54d15616d552df5d0 Merge "Merge remote-tracking branch 'origin/9.0' into qds/dev" into qds/dev
f8c99ebd9afc59cef8b4606b9c1c6e2ceca99d32 Merge remote-tracking branch 'origin/9.0' into qds/dev
583e53cb085db1dec0dbd648b4511d00f262277d QmlDesigner: Make DS find the qmlpuppet on macOS
b8d68b6f659315b03caa00a1b2b369c27a5cf18f Baremetal: Add st-util --connect-under-reset option
30312cc74adc59fb25f3026873b9dbdb6871041e Vcs: Proliferate FilePath a bit further
9c69a63b3b19ecac4d0dfb36678a40733dcc99da QtSupport: Simplify and FilePathify QtVersion::hasMkspec()
63022d08ea8f9f04a75d301602e639b83b1b7396 QmlDesigner: QmlPuppet as standalone tool
1beaa0771cf45abd04d62e5aa833d289fcf1019e Support temporarily dragging progress details out of the way
a8801eff5faa99104cc9016fe5d10f77ba2bc3c9 ProcessTestApp: Make setupSubProcess const
ac526d326cab6f19955cec4e19c73bc309bd96c8 QmlDesigner: Update qtbridge typo
4d1041169eceb630604eab8d25f257ae9d15a988 QmlDesigner: Update Version Number
c1f3dc4f5473cb3fa8cb59bf2c6d607a059c75c6 QmlDesigner: Add positioning methods to QmlVisualNode
0228cfef1c4d12b3c21a0b3b9bf1726af241512f QmlDesigner: Update the state group name
77e49a954ea85cb9fa2e1ccbde5ffc3992ea1be1 QmlDesigner: Workaround strange superClasses behavior
c5db6427911ae2d35af862f14ef8b7c88b6fc5b9 QbsProjectManager: Add UI support for "profile" build variant
d9d4d1829af35e8d01ca812825b0d413904abca1 QmlDesigner: Fix material name editing issues
3f2c3324fd2dd011b72ff59a25a1fa6ec2cfae1d QmlDesigner: Update the typo in Adobe illustrator export
442a916026941cc47e4e8859992a0e2c972dfe1d QmlDesigner: Update Adobe XD Information
074126cf47238029bab220de46a7c3f86cfbd392 ClangCodeModel: Fix usage type of constructors in "Find References"
15dd073e0c43c7c257684997fef033fad9089fd6 QmlDesigner: Add basic drag-n-drop support to textures
d5a7f25e608acdd1efd6c76554d7144f70454f02 QmlDesigner: Implement content library view
1c58642ae2c6a548c03c40ba93ec855861070e2d QmlDesigner: Add Fit Selected to 3d Context Menu
7dd4af1a345b76260ea8565266baf326890bd37d Merge "Merge remote-tracking branch 'origin/9.0'"
3e3569f6dca6f483c69c991040e1430a59836379 CPlusPlus: Add more usage tags
63c1bc572a24a7ab38a616554f330eb4c5e8647d Merge remote-tracking branch 'origin/9.0'
9febccb2d7764ed2dd133ce4992b0ff7311d8e86 QmlDesigner: Update material browser materials on state change
d891e18edc7864bdbff7cc0595c18add82c8c3f6 CPlusPlus: Make Usage::Type QFlags-based
0b1d26599197ceb32a2ebf15147281d9271678d9 Editor: use less bright colors in diff editor for dark color scheme
e8899baa2115f75cd26a9fdb8e8fd3c49e69fe1f QmlDesigner: Add support for CubeMapTexture to NodeMetaInfo
08f5a92bacfada366a7711ec9bd58031df1b257b QmlDesigner: Add Paste to 3d Context Menu
f113b05530fcd236309729ed760691fef32c9881 QmlDesigner: Reset possible imports on all open documents
e3bf1725852cbefd11b458ec7f3e816b8dedb5dc QmlDesigner: Handles directories in Project storage updater
a8c5cbaf94514f08a04baf18c5212444e6a6f3eb QmlDesigner: Remove duplicate include
40072fb5c7fc491b7c36c6288dd58ddcf736c4e6 Merge remote-tracking branch 'origin/9.0' into qds/dev
5779d036c511fb22fd73c3e090bd33a5be90be38 Wizard: Fix tooltip translation
f51162a3fe40a64adc3b606f22e1873268f31669 QmlDesigner: Improve ownership of QmlProjectItem
68388a38dd8e1ec06b48b779543097a9f8ba4156 Merge remote-tracking branch 'origin/9.0'
079906a122cae6dd0511a97c0f175488de5c06b5 Replace QML Profiler's RangeDetails close button by collapse button
a0623a02662ea38d9b96454d2b4701a505c5682d Drop static_cast from connections
118d9d43d2a053e2641c14a8f404ff202977c154 QmlDesigner: Rename default state group
e979579af56916340427c9faa548781c7101fd12 QmlDesigner: Add Edit Component Action to 3d context menu
c86b86b2546424dd6ec5bccf4b50ab04965fded4 Help: Remove duplicate results
9a79e530c83558887cfdafd96210d36b954d451f GenerateResource: Avoid code repetition
3897d917e21a45490bfdf2fddc322507811576a0 QmlDesigner: Add camera-view align actions to 3d context menu
0dfd2a2b0984d41160d3aba686388b0e6a548e54 QmlDesigner: Reactivate nodeinstance cache
9c3df49ca8c580b28f5b8910ac1170ca8bd733db QmlDesigner: Fix memory leak in 3D view context menu handling
b6e8fd3e91b46689703959cf2a0a75b50437632d QmlDesigner: Remove bundle import after last component is unimported
c1a247e7c1ee1b596b4fc5207327aaca0b0d5402 ProjectExplorer: Sort ABI selector boxes
af9ec8d20a3c70c233e3554a5f8eb6d7a1c70053 QmlDesigner: Add Copy to 3d Context Menu
73113519cf682740cbd9a0229776bb10e11eb946 QbsPM: Simplify toolchainList
c55ac53f3c8ab92be1fe146f6cf2d46c56118939 QmlDesigner: Hide bundle materials from components view
885268a3af013cb8e5df708b465056e980b08841 QmlDesigner: Optimize ModelPointer size
3c61470cb875d0377732783d2420b4d6477d36e5 CppEditor: Consider project part language
267bfdb0f98fa5e11a584d86e412578b435409fb Adapt to upstream change
2bbae7d499d16566f295a56682fef7d68f064a63 QmlDesigner: Detach views outside of model destructor
211f53cebae66608f080e856c3bb8ec1b3136e7c Prevent a crash on close
134c84b502846cd640c8fefe9949845f96db5686 ProjectExplorer: Remove MakeStep::jobCount()
a0835dd3d3e2dfcc96054a7c77ac9663837bd6f0 ClassView: Remove unneeded includes
d63bfa4a2975541b51fc8173fdb06933bb9dd40d VCS: Simplify submit editor accept/close flow
aa339276515da0b8c40193813de00d3cebfa1a3b Merge remote-tracking branch 'origin/9.0'
13f40f5471e55757a2cf9bba8d052750a2f2a753 Utils: Add sorted() function
55b8ab78460b41ea75fcc549db1ce3a6b9128690 cppeditor: make it possible to ignore patterns
001d55e1f0e8676acf4dbd0a309612f4b183f22f QmlDesigner: Fix empty texture source after drag to map property
866a658b42ea5984ea367da616396dc951559ba1 Get rid of unneeded includes of RunControl
a0a8bf224509e5647bc20561eb525715f2afcaac QmlDesigner: Fix puppet creation in 3D asset importing
b662c50e0a32e7444313758af935e03e20c51892 QmlDesigner: Fix node creation via 3D view context menu
8662470aba117c491f67e84adfc5f9d4dda107b5 CPlusPlus: Allow " = default" also on function implementations
5316a8f799eb8f3011799801a68c70cb522d7cd6 QmlDesigner: Make Formeditor remember the zoomlevel
8608386c3668169a4065f6313918469713ff649f CppEditor: Adapt include locations when renaming ui files
7092d8da221ba8cfb79eab994194656ec19e369d QmlDesigner: Fix ColorEditorPopup on Qt 6.4
b56de3d8beec853ef09c09ca9cbf8b1549cf0dbe QmlDesigner: Reset possible imports explicitly
1593f869ed0bc9e8341a62239800b00ad84cb669 VCS: Remove "Commit" option when closing commit editor
2f1f80152d74c5c2d8d071f4d96d6851dd313e0e VCS: Remove "Do not ask again" checkbox when closing commit editor
f4e18967e20822097c8bf448fe60b70a3ce17b10 CPlusPlus: Remove unused parser functions
d77a7167dbcde44ff719e6d7596d1a9bf3fd8df0 ProjectExplorer: Use LRU for sorting sessions in menu
07d30b8632fd73d2ff47f4e9e60c4935dbce4f5c QmlDesigner: Retrieve possible imports only once per project
a8e74a17388408a992c86afa5afad0b592afb674 QmlDesigner: Use new auxiliary properties
cd7a378418378949a6d208978b66fcc96795b290 Adapt to upstream changes
e2706436a902ea069ca537dde50a0ce0533d7ccc QmlDesigner: Fix Q_ASSERT
c23564c337dff6f44739de4537f7ff8538c4df21 Merge "Merge remote-tracking branch 'origin/9.0'"
e243b4746dcbc8f8b8045c28d4525d774136c8e1 QmlDesigner: Add missing environments
10773633f6ccc8f5b8553c0ae7d5bdd807cac86f Merge remote-tracking branch 'origin/9.0'
c2208fecfd586d2f3b7f733d7770ac62e3f19ff9 QmlDesigner: Rewriters parses immediately
d554bf5e2a035d234241075157232ad2e4fd94bb QmlDesigner: Remove check if setIdWithoutRefactoring throws
4e72e0fd27ada447cf32dd0e7aeb7ac44296e994 QmlDesigner: Improve defaults of external dependencies fake
f696d1e6cf01209fa3cd0cbdff989c654352c904 DiffEditor/VCS: Save document before applying/reverting patch chunk
7edf7435836d044e05cdf0493861c07f4e91d367 Merge remote-tracking branch 'origin/9.0'
f1fc14acc9c22f11211bcf9f7184e362a06a8a7b Merge "Merge remote-tracking branch 'origin/9.0'"
3bdb1a3edff718caf14ed0783b0868e75c10ae0f Merge remote-tracking branch 'origin/9.0'
2ba40d5ea9c99a3b5c40874cb4eb3d2c28923639 QmlDesigner: Move code to compile time
ac3a7991ff619f5747a6253985bdee263bb57826 DeviceUsedPortsGatherer: Simplify internals
d8987a1042d02bddda4b8108a8386492debd9afd qmljseditor: remove unneeded import
93c15f48dc38423041b00c55cbef885a73c9d025 Editor: ensure cursor is visible after handling backspace
890b85d17ecd5b72a87849a0bb83e99d1fd0edd7 VCS: Simplify promptSubmit a bit more
54604b56db2b1f2783940f11a109a3a031f776e4 VcsBase: Fix build
903fc1b1ba7b537c593de2d6097fa34ff470d4d7 VCS: Fix blaming previous revisions for renamed files
9b61e484bed797f0424440bce5c87172a832eda4 VCS: Deduplicate some submit editor closing logic
eaad78547cd915306ecfea380824b77f2907c3c4 VCS: Remove "Prompt to submit" settings
a450a6707e754237a23cff5a1268c39ac7dcd946 QmlDesigner: fix Qt6 detection
eb6601876797602770f19c239616315c44ca2bb5 QmlDesigner: Fix puppet paths
6ea1aecc463c3b612d70ad7720a67633adc1553b QmlDesigner: Use new validation approach in annotation list
b07cbe62bf759cf259cda4b0ae80a3c7e5121222 QmlDesigner: Use new validation approach in event list
e27af202fc264425049a065115bb1448f09a5170 qmlls: move isSematicInfoOutdated to private object
8f9d582c6cfdaf1ee4c0d5fcfba48ebc0506e97b DeviceUsedPortsGatherer: Move getNextFreePort() into PortList
8394bb0a2b6801405a48207f301f0efb66f2debe Git: Improve tracking of external changes to HEAD
e757122843ecb411ec2bd01b70137427a23ce6f6 VCS: Minor optimization in relative file resolving
424fd7c557907c03cb957fadc90cd81f87063f5d VCS: Resolve symlinks on blame
46df40a91904edf500e4cf5ab234f5054348258c VCS: Use more FilePath in VcsBaseEditor
3cbbe911679e3fb266ec5d44e886a52318e03ac7 Merge remote-tracking branch 'origin/9.0'
5d2e4f7d992ebe486ca0a3ab0e625896c6eb52a4 QmlJSEditor: Fix qbs build
19df320051f7354cad7e0f9c3e309f2e995a6412 Add support for new Quick3D profiling events
33a8854b2114507343d836494908a7dbe200c641 Fix build
b997d095a116621f0f9ac1e93dfb71515bce80a0 VCS: Linkify file names in diff output
ffd69f0428c2a183f1e04610af3801ccd8f01b61 QmlDesigner: Fix linking warning on mac
ba891e7f346d9d212f45d25ef7969842f41b13d2 QmlDesigner: Prevent calling the node instance view directly
21b0a69331c3e161893afaf8b27669544fc51b26 NanoTrace: Add missing dependency for Qt::Core
329fac55106a73d95c1ab0cdbef4b0597070c821 QmlDesigner: Add version number to puppet path
3baf305f9ba0192d662ab0ed44eac2910c1e4524 Automatic qmlls support (experimental)
5cc14a2453597c2edd16bccce6e090e3ca896f4a GitHub: Build against QtC 9 Beta
0a139439bbf4582c683f805afef75cb3e45deabc Merge remote-tracking branch 'origin/8.0' into 9.0
634ac23af7da3ac48c6fea9fc009ab191bc037e3 Merge "Merge remote-tracking branch 'origin/9.0'"
caa013b8e829e200a90c561cff3aeb3300dcc8d8 MesonProcess: Reuse ProcessProgress
8e4cdf2a6957d96ce826df27429596d475902765 VcsCommand: Some cleanup
eb8b8eea41508ccc08b46a3da28345ded5ad42a3 VcsCommand: Remove signals for communication with VcsOutputWindow
f0628c9b7d1f521392b05c2f98f87f095f7f8f69 ProcessResultData: Add a flag for forceful close
9cb9bb0635b31a46c791466e3a81f27fbaeaf94d VcsCommand: Reuse ProcessProgress
0c1ca4242f66a8302664d6f78ed3c6cc6dc42b92 Merge remote-tracking branch 'origin/9.0'
07ca7f7b990ca5343fb100910ed9d2006f4dbe3d Introduce ProcessProgress
907c36217d6242a27d54e27798cb6869788134da McuSupport: Make plugin UI translatable
894f1b81c82454c10e9f87907bfcb0e8bc32be08 UpdateInfoPlugin: Remove unneeded scopes
80938eae9a366285f9246f3a84e929bbc9616459 UpdateInfoPlugin: Use FilePath for maintenance tool
92d84738cfe5dfd01a631f9187d4301dd2c884c9 VcsCommand: Call GlobalFileChangeBlocker functions directly
07931849565fc5f7a6699a74f07a2052d781cea8 Merge remote-tracking branch 'origin/9.0'
5d8e6ac853c5f4de8cd7596dff8e3e07aac4145c Update qlitehtml to a version w/o qAsConst
7efb0a0625bb87c73519292fcf5a0e104e51bc93 Merge remote-tracking branch 'origin/9.0'
fb643db0885ebefffe86f876fdbb6d403f5baafe McuSupport: Add flag in JSON for adding a package path to system path
78cf5051f51ab7399a7d04d7c16d2da96b2a4012 QmlDesigner: Merge cmake files
ef573e44073262cd6383e371a257359604369502 Do some cleanup
e6bfa33be0ebb253e1e6761d5318032aa7224b79 Avoid returning value by reference
70466ed81191e4fb40d30b8d3078ff0247018b0c Replace foreach with ranged for loop
28c507eccb789e53a748784e9ec2de100b56e608 Simplify data structures
7ffdada4a3a0ce02371135813015aeb838c89c76 Adapt to upstream changes
8b028a69a7ecad9f5730485e5160588d4755fa5d Bump version to 10.0.0-beta1
1b83c88eebe6521ba680b909976594ec9fc4117d Core: make options page keywords accessible
d5e12ddac406e0527fc8a01f89d3ae2fe3e92552 QmlDesigner: Fix puppet starter
571d822c7b5d270a3017ffddb6b779dcac488d60 QmlDesigner: Fix missing validation
bb71aa6d43fc3fd5b2642e476f91b12e3503a718 QmlDesigner: Prepare split of puppet creator
f195c3a065e560dec98ae33b1ab3d75d008ae5da Merge remote-tracking branch 'origin/9.0'
5f1e4418deb1739dc0244602332265ed28f79f74 Sanitize: Add sanitize flags to shared libraries
9cea8ab213c789e6bb832b0e371bd018396219f5 Adapt to upstream change
294f2831920bbd86f4a7445aeccb17e3add27003 Adapt to upstream changes
3e322ba3a679ee58287b17e8e624b8913263ee8f Adapt to upstream changes
ec32aa0427f4d725cfca04c2d5b951fd4713589c Adapt to upstream change
56638b69a83e602b9462cc75ce78284d27f2e41b Fix compile
0bfc4424ff36f1783498b5dd74b481a2337d2a2a Adapt to upstream changes
810517d9c9996b26adbdebf02bb963cfdd9c93ea Adapt to upstream changes
91a82da6b730fb0ae2f7af421a4993a713a1d6a7 Adapt to upstream change
b5af8e957f153fe5be6c6548b179a13df748b302 Adapt to upstream change
c06a40afcded4f00561316418c2802b93eb49593 Adapt to upstream changes
253d9a02f6157b0a971504734a92853ae51e45d8 GitHub: Build against Qt Creator 8 release
8060ca52041b780ecc5d38f3b8704f6c2417a4df Adapt to upstream changes
d44056f9d861b0f3794a1b593c89c92f3b8bbffb Adapt to upstream changes
b893eaa2b2ed688b949b9898c23a0bbdc8e359cb Merge remote-tracking branch 'origin/8.0'
a8fa3310ee6dcccaae57dad45ec49f5cc84f42a5 GitHub: Build against Qt Creator 8 Beta2
1088bc93e27306d21dc65fe54789fe01d999322a GitHub: Build against Qt Creator 8 beta1
f46df201aa73bef328b1b533da14e4496251fd88 Adapt to upstream change
8cec0274a50d756f9689674f4be569e616976764 Adapt to upstream changes
d68b17e7eaabdf45985fc8245ca7a04218440aaf Merge remote-tracking branch 'origin/6.0' into 7.0
1eb9263e315107fdd977c085282c6b8e2e41d691 GitHub: Use checkout@v2
02804556234e88866ac174fc8ea4b91f8b35ac5a Remove qmake build files
9a351ad5f8a2a85c13eea40fa0a7fc4399b098af GitHub: Bump to QtC 6.0 and Qt 6.2.1
796bcbf4202b28df5b622778b87a04d33c4bc953 Doc: Fix dependency to Qt Creator
f37bd2436d2a2349718f69f67e9ca3e49094b977 Adapt to upstream change
71f975de85fd4c887b4dec4fe1b66776904e826c GitHub: Build against Qt Creator 6 snapshot with Qt 6.2
8b3d718b3a303a4a9ba18f358680a931fb918967 Adapt to upstream change
b5d598956d8e2dfccd03244494b9934b2491e64f Adapt to upstream changes
9bfb1f6bd930ec2944e6a07e7433fddcda435025 Merge remote-tracking branch 'origin/5.0'
34c39db79dbed008b76535d6bba3a4e3df90bd1f GitHub: Update Qt Creator, cmake & ninja, adapt build_plugin.py location
439d8cc52a65bf5fb45022c689dd8700dbf5e6f8 Adapt to upstream findTopLevelForFile change
d71e3fd441faba376475505a2498463a06febcb0 Adapt to IVersionControl FilePath changes
c7797cfa5bc7587ca53f686c83e82fc5805ed5f5 Adapt to upstream changes
395af1b83ecd79f35e7188e0b8e82d4eb0a1f23d Fossil: Adapt to Make SynchronousProcess results more directly accessible
5a134b4577e55f8589326126e34f5ec805f7e25d Fossil: Drop unneeded QRegExp include
718a9b0d4ce65998fb44247e86a771de5976297e Fossil: Adapt to upstream SynchronousProcess changes
7fd9cde17ac6d1c6f140c55b491b2fca00e2dd78 Fossil: Revert some unintentional hunk
ea431eede1e5deada06e1cc89ef92775de7bc01e Fossil: Aspectify settings
c973fae866382d47a70034641de28afb91bad4cc Update github workflow to 4.15.0-beta1 snapshots
f618a6dacbe668a9f916dd89b376aa50bb0c7091 Merge remote-tracking branch 'origin/4.14'
f55fc3c3bc73444c0986d520457055368ba7a32f Update github workflow for latest Qt Creator release
8f226483cabfc098077d348e2fe38fc2280ceb2f CMake Build: Increase cmake minimum version to 3.10
b5732298335e74d8d84b4768de863c5f7d218415 Fix compilation with upstream changes
ba97b2db1645d4c45cd937ab9919b7b1c34b7db9 Update GitHub workflow
9ec698eb0835ad928d8d6161e4a46c3f1b5faee1 Merge remote-tracking branch 'origin/4.13' into 4.14
9a1423bf05a5ecd560055ef32aa981e7c2d813c6 Do not include Core::Id
3fb58390ab786e3047f224933870a9a3dd60f95a Use C++17 like upstream Qt Creator
a3b0e885c4be19b99d0d7203cc09a31d6a964cd5 Add github action
1b32611ec44ada0a11b15507b719451b8ce32728 Merge remote-tracking branch 'origin/4.13'
69f3f359d7304db432c137784d3f17a8fb1d37ec Work around MSVC error initializing QString from const char *
9a4a89050d26346769ecf9ebf69299d0dfaf7861 Merge remote-tracking branch 'origin/4.13'
eedbcde445c4bd006480297fd6f99e592e297e01 Use Utils::SkipEmptyParts instead of the deprecated one in QString
1e3721c8742171ba0a34dfe36636d24fe69fbd5b Use Utils::SkipEmptyParts instead of the deprecated one in QString
a8c30c9253a0310a4f6eb4e5b26e9e793d0ebd64 Merge remote-tracking branch 'origin/4.13'
a0f4ce71e81dcf40e6c4c04f0fd87749ae71fd42 Work around MSVC error initializing QString from const char *
cef9a471e64cb9bac4d88923ca4a96c84f0f92ca Fix build after upstream change
6ab75271a8e7d07bd3456493d4749bf7094bd3be Adapt to upstream change
53fc320b7264e19a5ae12ede2c120acaa151240b Adapt to upstream changes
3d7697667d431a678ebddc96e8fc0ae78755ef2d Adapt to Fossil client version 2.12
30b04ca9190f139498f59b7e75eea19933ceec4f Push/pull: Fix the handling of the Default URL
9a64faff10162200c31babbccb3a1d9d6acd082d Import wizard: Fix the auto pre-fill of the 'Local clone' name
614a2e5f1b52557f5b3776544f59471cdeecffd2 Merge remote-tracking branch 'origin/4.12'
b6908dee096c62ff4f84e87bfaf89b5841fae63a Adapt to 4.12
2ccf5c1154c3f24ad0d82d7f7b30d3b594302287 Adapt to upstream changes
57e8de1e0b84f1f837773ca7e6feff2a4457886c Adapt to 4.12
86f7c7bf12703d9af75486b807a911b9daf70f35 Adapt to recent VCS base changes
4a9b113c5c27e818db8d6aa6bc03f4eafa296fe8 Adapt to upstream changes
7855122c5398a01f3d8e0e6d1885a2e723106871 Fix crash on startup
cf8dd5cb3c40b8a071220dfa2e319eb8bdd2df48 Fix CMake and Qbs build
d5c720379e4781d1a7f959f17548b77775115750 Use default constructed QFlags
a565e05fb047fbdaaeadbfecc5ffbbfefe80d2f2 Adapt to upstream changes
b6f137afef222bfde9b13f1685cde9737f4c5a2b Adapt to upstream changes
b418e205cf3661a491afa5f608a612dee316141b Adapt to upstream changes
f199d6f57db8a1b4ae31bbbf0ff959211ca5f6a5 Merge remote-tracking branch 'origin/4.11'
6296335bf98d515ad97bc86027969873835cf075 Adapt to changes in 4.11
21f9cfd09737c635b9a83181baa9c7bd47aa75ac Merge remote-tracking branch 'origin/4.10'
06a392feb419b85f03546ac77a39cb22700dd25a Add cmake build files
18841cc0f5c5d74f2830510a454f71350561e6a1 Adapt to upstream FileName changes
adf0b8f87f9f36a93d5248465bb85d59883843b2 FossilClient: De-noise
a1c0d912b273d6c5e902d97f9fbebee1fd8cae0b Adapt to master
00821627645cb826ac14884a0b262e5b92ac9c18 Merge remote-tracking branch 'origin/4.9'
5378134c2154dbd09140bf78c43a130f6fffe219 Adapt to 4.9
a2a55674e353c81c7150eaadb57a4cd554ffe354 Doc: Fix dependency to Qt Creator
fd26cf5c8daff0571c5530a004d547495eba1ba9 Merge remote-tracking branch 'origin/4.8'
a155f69ca90657b6d8e042514b98a4c707ffe712 Adapt to master
4d0a7377c0f749ba0a58bf4b996c8397fb2cb2c1 Create a correct editor for 'Log Current File'
c75bb148e8e0c77458914c543be1cbe00388d552 Add Reload button to Log and Diff editors
70e7cf36b46aea39ab6d548b2e627ea597a4c9a7 Add .gitignore
10cb61f3cff1f2300a4af2916eff8c27717fb347 Fix build when using QTC_SOURCE environment variable
bb6da94e5dc3d29b57690e9ead33e67ff56c513e Suppress command output on revision query
d3998c6319e4f54a18d3627912dc97834f31ab9b Adapt to upstream VCS change
3cc4d744656310ad5fe4f7e9441d3051dca004d3 Merge remote-tracking branch 'origin/4.7'
88e11284ea0db367ce763388efcc9b566684f65c Add support for annotation of any given revision
b1e1cdef867f73b1a999e583c09de61506509911 Allow to specify the plugin source commit-id used in the build
e74016a32f95521e59e8b0f178d495edddb4eddf Update clone wizard icons
15007920a39a3e8adf74711268e1f7811e57b8ab Update to use supportedProjectType
ef3c8a4d2daa6fda1b7d2dca794c78c0d2387b51 Merge remote-tracking branch 'origin/4.7'
06d73b2e24249290e68a095e7a69a1cab172b4f8 Update copyright year
e32b8e4ca28801a77eedb843ec6d64fff4d6a31c Adapt to QtCreator 4.7
eb8076bd3af33abaf59545310f9152355604a101 Merge remote-tracking branch 'origin/4.5'
ee7505444c0b63e560ca6d076599442a4825f3af Fix isVcsFileOrDirectory
afff1e214f69fbec167c34f69029c878f44512d3 Merge remote-tracking branch 'origin/4.5'
ed94e8bda218d8cd043d09f11ef4806f0b150494 Merge remote-tracking branch 'origin/4.4' into 4.5
f6cea871f1e6edeb3cb005adb82db19b3f25095e Adapt to change in VCS registration
bf3f576c4a3c75a60cb2c6b575b41d58374db260 Adapt to changes in VcsBase
cb9135ade9c2c3c095213e261dca2566c2a80beb Add missing override
972a2385833e4f2ce1163cfd16d316ee514ddb17 Merge remote-tracking branch 'origin/4.3'
8cc9b5c57d453ea0de7fe9838b22824c7f9eec21 Vcs: Jump to the current source line in Fossil Annotate editor
ac8005190d5fa26d3488392aa560a3afd196e99c Vcs: Re-implement Fossil's topic indicator using a TopicCache sub-class
574c5d35667de559193e94a571727da78b5ca968 Merge remote-tracking branch 'origin/4.3'
9086d2d11de6b0543bcae41af1be6e06a5a5f65e Adapt to changes in docs.pri
f8b69403ec7cef38e1d071eb51700eae5752df78 Merge remote-tracking branch 'origin/master' into 4.3
ecf70972e87599b8a18347cc75746da6868a0a64 Doc: Add a doc project for the Fossil plugin
0d957a88f9fb68a63b9e4ac26857aca82006371b Add Qbs file
dea25a6b62d97ddbe4a94c028c3431d91dac5b86 Vcs: Add Fossil SCM integration plugin
21580dae57709995363cc4e8d50ec4f14025c59c Initial empty repository
```
