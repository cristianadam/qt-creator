---@meta

---The base class of all layout items
---@class LayoutItem
LayoutItem = {}

---Attaches the layout to the specified widget
---@param widget QWidget
function LayoutItem:attachTo(widget) end

---Column layout
---@class Column : LayoutItem
Column = {}

---@param children LayoutItem|string|BaseAspect|function
---@return Column
function Column(children) end

---A group box with a title
---@class Group : LayoutItem
Group = {}

---@return Group
function Group(children) end

---@param children LayoutItem|string|BaseAspect|function
---@return Group
function Group(children) end

---Row layout
---@class Row : LayoutItem
Row = {}

---@param children LayoutItem|string|BaseAspect|function
---@return Row
function Row(children) end

---Flow layout
---@class Flow : LayoutItem
Flow = {}

---@param children LayoutItem|string|BaseAspect|function
---@return Flow
function Flow(children) end

---Grid layout
---@class Grid : LayoutItem
Grid = {}

---@param children LayoutItem|string|BaseAspect|function
---@return Grid
function Grid(children) end

---Form layout
---@class Form : LayoutItem
Form = {}

---@param children LayoutItem|string|BaseAspect|function
---@return Form
function Form(children) end

---An empty widget
---@class Widget : LayoutItem
Widget = {}

---@param children LayoutItem|string|BaseAspect|function
---@return Widget
function Widget(children) end

---A stack of multiple widgets
---@class Stack : LayoutItem
Stack = {}

---@param children LayoutItem|string|BaseAspect|function
---@return Stack
function Stack(children) end

---A Tab widget
---@class Tab : LayoutItem
Tab = {}

---@param children LayoutItem|string|BaseAspect|function
---@return Tab
function Tab(children) end

---A Multiline text edit
---@class TextEdit : LayoutItem
TextEdit = {}

---@param children LayoutItem|string|BaseAspect|function
---@return TextEdit
function TextEdit(children) end

---A PushButton
---@class PushButton : LayoutItem
PushButton = {}

---@param children LayoutItem|string|BaseAspect|function
---@return PushButton
function PushButton(children) end

---A SpinBox
---@class SpinBox : LayoutItem
SpinBox = {}

---@param children LayoutItem|string|BaseAspect|function
---@return SpinBox
function SpinBox(children) end

---A Splitter
---@class Splitter : LayoutItem
Splitter = {}

---@param children LayoutItem|string|BaseAspect|function
---@return Splitter
function Splitter(children) end

---A Toolbar
---@class ToolBar : LayoutItem
ToolBar = {}

---@param children LayoutItem|string|BaseAspect|function
---@return ToolBar
function ToolBar(children) end

---A TabWidget
---@class TabWidget : LayoutItem
TabWidget = {}

---@param children LayoutItem|string|BaseAspect|function
---@return TabWidget
function TabWidget(children) end

---A "Line break" in the layout
function br() end

---A "Stretch" in the layout
function st() end

---An empty space in the layout
function empty() end

---A horizontal line in the layout
function hr() end

---Clears the margin of the layout
function noMargin() end

---Sets the margin of the layout to the default value
function normalMargin() end

---Sets the margin of the layout to a custom value
function customMargin(left, top, right, bottom) end

---Sets the alignment of the layout to "Form"
function withFormAlignment() end

---Sets the title of the parent object if possible
function title(text) end

---Sets the text of the parent object if possible
function text(text) end

---Sets the tooltip of the parent object if possible
function tooltip(text) end

---Sets the size of the parent object if possible
function resize(width, height) end

---Sets the stretch of the column at `index`
function columnStretch(index, stretch) end

---Sets the spacing of the layout
function spacing(spacing) end

---Sets the window title of the parent object if possible
function windowTitle(text) end

---Sets the field growth policy of the layout
function fieldGrowthPolicy(policy) end

---Sets the onClicked handler of the parent object if possible
function onClicked(f) end

---Sets the onTextChanged handler of the parent object if possible
function onTextChanged(f) end
