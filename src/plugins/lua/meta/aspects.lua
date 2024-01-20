---@meta

---The base class of all aspects
---@class BaseAspect
BaseAspect = {}

---@class AspectCreate
---@field settingsKey string The settings key of the aspect
---@field displayName string The display name of the aspect
---@field labelText string The label text of the aspect
---@field toolTip string The tool tip of the aspect
AspectCreate = {}

---The base class of most typed aspects
---@generic T
---@class TypedAspect<T> : BaseAspect
---@field value T The value of the aspect
---@field volatileValue T The temporary value of the aspect
---@field defaultValue T The default value of the aspect
TypedAspect = {}

---@generic T
---@class TypedAspectCreate<T> : AspectCreate
---@field defaultValue `T` The default value of the aspect
TypedAspectCreate = {}

---A container for aspects
---@class AspectContainer : BaseAspect
AspectContainer = {}

---Options for creating an AspectContainer
---@class AspectContainerCreate<K, V>: { [K]: V }
---@field autoApply boolean Whether the aspects should be applied automatically or not
AspectContainerCreate = {}


---Create a new AspectContainer
---@param options AspectContainerCreate
---@return AspectContainer
function AspectContainer.create(options) end

---A aspect containing a boolean value
---@class BoolAspect : TypedAspect<boolean>
BoolAspect = {}

---@class BoolAspectCreate : TypedAspectCreate<boolean>
BoolAspectCreate = {}

---Create a new BoolAspect
---@param options BoolAspectCreate
---@return BoolAspect
function BoolAspect.create(options) end
