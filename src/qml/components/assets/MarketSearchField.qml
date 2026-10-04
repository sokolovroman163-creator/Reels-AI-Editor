import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// The Market's one entry point: a pill-shaped field with no buttons. Enter submits, the
// trailing × clears the text or, while a request is out, cancels it. The pill is the only
// fully rounded control on the page, which is what marks it as the place to start.
Rectangle {
    id: root

    property alias text: input.text
    property alias placeholderText: input.placeholderText
    property bool busy: false

    signal submitted()
    signal cancelRequested()

    function focusField() { input.forceActiveFocus() }

    implicitHeight: Theme.touchUi ? Theme.controlHeight + Theme.spacingLg : Theme.controlHeight + Theme.spacingSm
    radius: height / 2
    color: Theme.panelAccent
    border.width: input.activeFocus ? Theme.borderWidthFocus : 0
    border.color: Theme.focusRing

    IconGlyph {
        id: lens
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingXl
        anchors.verticalCenter: parent.verticalCenter
        glyph: root.busy ? Theme.icons.spinner : Theme.icons.search
        iconSize: Theme.iconSizeMd
        iconColor: Theme.mutedForeground

        RotationAnimator on rotation {
            running: root.busy
            loops: Animation.Infinite
            from: 0
            to: 360
            duration: 900
            onRunningChanged: if (!running) lens.rotation = 0
        }
    }

    TextField {
        id: input
        anchors.left: lens.right
        anchors.right: clear.visible ? clear.left : parent.right
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: clear.visible ? 0 : Theme.spacingXl
        anchors.verticalCenter: parent.verticalCenter
        background: null
        leftPadding: 0
        rightPadding: 0
        color: Theme.panelForeground
        placeholderTextColor: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
        selectByMouse: true
        selectedTextColor: Theme.primaryForeground
        selectionColor: Theme.primary
        Keys.onReturnPressed: root.submitted()
        Keys.onEnterPressed: root.submitted()
        Keys.onEscapePressed: (event) => {
            if (root.busy)
                root.cancelRequested()
            else if (input.text.length > 0)
                input.text = ""
            else
                event.accepted = false
        }
    }

    IconButton {
        id: clear
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSm
        anchors.verticalCenter: parent.verticalCenter
        visible: root.busy || input.text.length > 0
        glyph: Theme.icons.x
        iconSize: Theme.iconSizeMd
        tooltip: root.busy ? qsTr("Cancel") : qsTr("Clear")
        onClicked: {
            if (root.busy) {
                root.cancelRequested()
                return
            }
            input.text = ""
            input.forceActiveFocus()
        }
    }
}
