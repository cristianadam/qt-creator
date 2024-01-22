---@meta

---A QWidget, see https://doc.qt.io/qt-6/qwidget.html
---@class QWidget : QObject
---@field acceptDrops boolean
---@field accessibleDescription string
---@field accessibleName string
---@field autoFillBackground boolean
---@field baseSize QSize
---@field childrenRect QRect
---@field childrenRegion QRegion
---@field contextMenuPolicy QtContextMenuPolicy
---@field cursor QCursor
---@field enabled boolean
---@field focus boolean
---@field focusPolicy QtFocusPolicy
---@field font QFont
---@field frameGeometry QRect
---@field frameSize QSize
---@field fullScreen boolean
---@field geometry QRect
---@field height integer
---@field inputMethodHints QtInputMethodHints
---@field isActiveWindow boolean
---@field layoutDirection QtLayoutDirection
---@field locale QLocale
---@field maximized boolean
---@field maximumHeight integer
---@field maximumSize QSize
---@field maximumWidth integer
---@field minimized boolean
---@field minimumHeight integer
---@field minimumSize QSize
---@field minimumSizeHint QSize
---@field minimumWidth integer
---@field modal boolean
---@field mouseTracking boolean
---@field normalGeometry QRect
---@field palette QPalette
---@field pos QPoint
---@field rect QRect
---@field size QSize
---@field sizeHint QSize
---@field sizeIncrement QSize
---@field sizePolicy QSizePolicy
---@field statusTip string
---@field styleSheet string
---@field tabletTracking boolean
---@field toolTip string
---@field toolTipDuration integer
---@field updatesEnabled boolean
---@field visible boolean
---@field whatsThis string
---@field width integer
---@field windowFilePath string
---@field windowFlags QtWindowFlags
---@field windowIcon QIcon
---@field windowModality QtWindowModality
---@field windowModified boolean
---@field windowOpacity double
---@field windowTitle string
---@field x integer
---@field y integer
QWidget = {}

---@return boolean
function QWidget:close() end
function QWidget:hide() end
function QWidget:lower() end
function QWidget:raise() end
function QWidget:repaint() end
function QWidget:setDisabled(disable) end
function QWidget:setEnabled(enabled) end
function QWidget:setFocus() end
function QWidget:setHidden(hidden) end
function QWidget:setStyleSheet(styleSheet) end
function QWidget:setVisible(visible) end
function QWidget:setWindowModified(modified) end
function QWidget:setWindowTitle(title) end
function QWidget:show() end
function QWidget:showFullScreen() end
function QWidget:showMaximized() end
function QWidget:showMinimized() end
function QWidget:showNormal() end
function QWidget:update() end


---A QDialog, see https://doc.qt.io/qt-6/qdialog.html
---@class QDialog : QWidget
QDialog = {}

---@return QDialog
function QDialog() end
