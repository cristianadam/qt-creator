/****************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/


// simple example

function printNode(node) {
   console.log("Select: "+ node.displayName() + " (" + node.internalId() + ")")
   console.log("    Properties: " + node.propertyNames())
}

function checkMetaInfo() {
   var ok = true

   for (i = 0;i < View.allModelNodes().length; i++) {
      var node = View.allModelNodes()[i]

      if (!node.hasMetaInfo())
        ok = false
   }
   console.log("Meta info is ok for all nodes: " + ok)
}

// We also can connect to callbacks
View.selectionChanged.connect(function() {
    var node = View.firstSelectedModelNode()
    if (node.isValid()) {
      console.log("Callback: Selection Changed")
      printNode(node)
    }
});

console.log('Start Test');

var rootNode = View.rootModelNode

console.log("Type of Root Node: " + rootNode.type())

console.log("Number of ModelNodes" + View.allModelNodes().length)

console.log("Properties of Root Node: " + rootNode.propertyNames())

console.log("Number of direct children of Root Node: " + rootNode.directSubModelNodes().length)

checkMetaInfo()

console.log("Select all direct children")

var i = 0

for (i = 0;i < rootNode.directSubModelNodes().length; i++) {
   var node =  rootNode.directSubModelNodes()[i]

   node.selectNode()
}

console.log("Select all nodes")

for (i = 0;i < View.allModelNodes().length; i++) {
   var node = View.allModelNodes()[i]

   node.selectNode()
}


// C:\dev\tqtc-qtc-super\qtcreator\tests\manual\qml\scripts

