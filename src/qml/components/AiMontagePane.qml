import QtQuick
import QtQuick.Controls.Basic
import Drift

Item {
    id: root
    signal doneRequested()
    signal settingsRequested()
    signal addonsRequested()
    property string preset: "beauty"
    property var videos: []
    property var selected: ({})
    property string selectionError: ""
    property bool whisperReady: Addons.hasKind("whisper-model")
    Connections { target:Addons; function onCatalogChanged() { root.whisperReady = Addons.hasKind("whisper-model") } }
    readonly property var presets: [
        {id:"beauty",label:qsTr("Beauty / Lashmaker")}, {id:"before_after",label:qsTr("Before / after")},
        {id:"expert",label:qsTr("Expert")}, {id:"talk",label:qsTr("Talking")},
        {id:"dynamic",label:qsTr("Dynamic")}, {id:"minimal",label:qsTr("Minimal")},
        {id:"music",label:qsTr("To music")}, {id:"process",label:qsTr("Work process")}
    ]
    function refreshVideos() {
        const list = [], chosen = ({})
        for (let i=0;i<AssetLibrary.count;++i) {
            const asset = AssetLibrary.assetAt(i)
            if (asset.kind !== "video") continue
            list.push({id:asset.id,name:asset.name,pending:AssetLibrary.isImportPending(asset.id)})
            if (selected[asset.id] !== undefined) chosen[asset.id] = selected[asset.id]
        }
        let count = Object.keys(chosen).filter(id => chosen[id]).length
        for (let i=list.length-1;i>=0;--i) if (chosen[list[i].id] === undefined) { chosen[list[i].id] = count < 20; if (chosen[list[i].id]) ++count }
        videos = list; selected = chosen
    }
    function selectedIds() { return videos.filter(v => selected[v.id]).map(v => v.id) }
    function selectedPending() { return videos.some(v => selected[v.id] && v.pending) }
    function toggle(id,checked) { const copy = Object.assign({},selected); copy[id] = checked; selected = copy }
    Component.onCompleted: refreshVideos()
    Connections {
        target: AssetLibrary
        function onCountChanged() { root.refreshVideos() }
        function onImportingChanged() { root.refreshVideos() }
        function onBadgeRevisionChanged() { root.refreshVideos() }
    }
    Timer { interval:1000; running:root.visible && root.selectedPending(); repeat:true; onTriggered:root.refreshVideos() }
    Flickable {
        id: scroll
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: actions.top
        anchors.bottomMargin: Theme.spacingLg
        contentHeight: form.implicitHeight + Theme.spacingLg
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }
        Column {
            id: form
            width: parent.width
            spacing: Theme.spacingLg
            ThemedLabel { id: requestLabel; width:parent.width; wrapMode:Text.WordWrap; size:"base"; text:qsTr("What should I do with these videos?") }
            TextArea {
                id: requestField
                width: parent.width
                height: Math.max(160,implicitHeight)
                wrapMode: TextEdit.Wrap
                color: Theme.panelForeground
                placeholderTextColor: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                padding: Theme.spacingLg
                enabled: !ReelsAI.busy
                Accessible.name: requestLabel.text
                placeholderText: qsTr("Make a premium 20-second beauty Reel. Start with the result, show the process, add Russian captions and a booking CTA.")
                background: Rectangle { color:Theme.panelAccent; radius:Theme.radiusMd; border.color:requestField.activeFocus ? Theme.focusRing : Theme.panelBorder; border.width:1 }
            }
            Flow {
                width: parent.width
                spacing: Theme.androidTouchGap
                enabled: !ReelsAI.busy
                Repeater {
                    model: root.presets
                    ThemedButton {
                        required property var modelData
                        height: Theme.androidMinTouchTarget
                        text: modelData.label
                        variant: root.preset === modelData.id ? "primary" : "secondary"
                        onClicked: root.preset = modelData.id
                    }
                }
            }
            ThemedLabel { text:qsTr("Videos selected: %1 / 20").arg(root.selectedIds().length) }
            ThemedButton {
                width: parent.width
                height: Theme.androidMinTouchTarget
                text: qsTr("Import videos from phone")
                enabled: !ReelsAI.busy && !AssetLibrary.importing
                onClicked: {
                    const urls = FileDialogs.openFiles(qsTr("Choose 2–20 videos"),[AssetLibrary.mediaNameFilter()])
                    if (urls.length > 20) { root.selectionError = qsTr("Choose no more than 20 videos at a time."); return }
                    if (urls.length > 0) { root.selectionError = ""; AssetLibrary.importUrlsAsync(urls) }
                }
            }
            ThemedLabel { width:parent.width; wrapMode:Text.WordWrap; text:root.selectionError; visible:text.length>0 }
            Repeater {
                model: root.videos
                ThemedCheckBox {
                    required property var modelData
                    width: parent.width
                    height: Math.max(Theme.androidMinTouchTarget,implicitHeight)
                    text: modelData.name + (modelData.pending ? " — " + qsTr("Importing…") : "")
                    checked: !!root.selected[modelData.id]
                    enabled: !ReelsAI.busy
                    onToggled: root.toggle(modelData.id,checked)
                }
            }
            ThemedLabel { text:qsTr("Duration") }
            ThemedComboBox { id:duration; width:parent.width; height:Theme.androidMinTouchTarget; model:[15,20,30,45,60]; currentIndex:1; enabled:!ReelsAI.busy; displayText:qsTr("%1 seconds").arg(model[currentIndex]) }
            ThemedLabel { text:qsTr("Aspect ratio") }
            ThemedComboBox { id:aspect; width:parent.width; height:Theme.androidMinTouchTarget; model:["9:16 Reels","1:1","16:9"]; enabled:!ReelsAI.busy }
            ThemedLabel { text:qsTr("Captions") }
            ThemedComboBox { id:captions; width:parent.width; height:Theme.androidMinTouchTarget; model:[qsTr("Auto"),qsTr("Russian"),qsTr("Off")]; currentIndex:1; enabled:!ReelsAI.busy }
            ThemedLabel {
                width:parent.width; wrapMode:Text.WordWrap
                visible:captions.currentIndex !== 2 && !root.whisperReady
                text:qsTr("Install the local Whisper addon to generate captions. The agent will report if a required addon is missing.")
            }
            ThemedButton { height:Theme.androidMinTouchTarget; text:qsTr("Open Addon Manager"); visible:captions.currentIndex!==2 && !root.whisperReady; enabled:!ReelsAI.busy; onClicked:root.addonsRequested() }
            ThemedSwitch { id:planFirst; width:parent.width; text:qsTr("Show the plan first"); checked:true; enabled:!ReelsAI.busy }
            ThemedSwitch { id:quality; width:parent.width; text:qsTr("Maximum quality"); enabled:!ReelsAI.busy }
            ThemedSwitch { id:previews; width:parent.width; text:qsTr("Allow selected frames/previews to be sent to Polza"); checked:true; enabled:!ReelsAI.busy }
            ThemedLabel { width:parent.width; wrapMode:Text.WordWrap; text:qsTr("Editing stays on this device. Your prompt, project context and allowed previews are sent to Polza. Original videos are not uploaded.") }
            ThemedButton { height:Theme.androidMinTouchTarget; text:qsTr("AI settings — Polza.AI"); enabled:!ReelsAI.busy; onClicked:root.settingsRequested() }
            ThemedLabel { width:parent.width; wrapMode:Text.WordWrap; text:ReelsAI.stage; visible:text.length>0; size:"base" }
            ThemedLabel { width:parent.width; wrapMode:Text.WordWrap; text:qsTr("Step %1 / %2 · %3").arg(ReelsAI.step).arg(ReelsAI.maxAgentSteps).arg(ReelsAI.activeModel); visible:ReelsAI.busy }
            ThemedLabel { width:parent.width; wrapMode:Text.WordWrap; text:ReelsAI.plan; visible:ReelsAI.reviewingPlan }
            ThemedLabel { width:parent.width; wrapMode:Text.WordWrap; text:ReelsAI.result; visible:text.length>0 }
            ThemedLabel { width:parent.width; wrapMode:Text.WordWrap; text:qsTr("Undo AI returns the project to its state before this run, including any edits made afterwards."); visible:ReelsAI.canUndo }
        }
    }
    Column {
        id: actions
        anchors.left:parent.left; anchors.right:parent.right; anchors.bottom:parent.bottom
        spacing:Theme.androidTouchGap
        ThemedButton {
            width:parent.width; height:Theme.androidMinTouchTarget
            text:ReelsAI.reviewingPlan ? qsTr("Assemble") : qsTr("Make Reel")
            variant:"primary"
            enabled:ReelsAI.reviewingPlan || (!ReelsAI.busy && !ReelsAI.checkingConnection && requestField.text.trim().length>0 && root.selectedIds().length>0 && root.selectedIds().length<=20 && !root.selectedPending() && !AssetLibrary.importing)
            onClicked: ReelsAI.reviewingPlan ? ReelsAI.assemblePlan() : ReelsAI.start(requestField.text,root.preset,duration.model[duration.currentIndex],aspect.currentIndex===0 ? "9:16" : aspect.currentText,["auto","ru","off"][captions.currentIndex],planFirst.checked,quality.checked,previews.checked,root.selectedIds())
        }
        Flow {
            width:parent.width; spacing:Theme.androidTouchGap
            ThemedButton { height:Theme.androidMinTouchTarget; text:qsTr("Stop"); visible:ReelsAI.busy; onClicked:ReelsAI.stop() }
            ThemedButton { height:Theme.androidMinTouchTarget; text:qsTr("Change plan"); visible:ReelsAI.reviewingPlan; onClicked:ReelsAI.changePlan() }
            ThemedButton { height:Theme.androidMinTouchTarget; text:qsTr("Undo AI montage"); enabled:ReelsAI.canUndo; onClicked:ReelsAI.undoMontage() }
            ThemedButton { height:Theme.androidMinTouchTarget; text:qsTr("Back to timeline"); enabled:!ReelsAI.busy; onClicked:root.doneRequested() }
        }
    }
}
