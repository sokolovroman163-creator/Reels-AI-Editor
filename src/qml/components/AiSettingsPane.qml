import QtQuick
import QtQuick.Controls.Basic
import Drift

Column {
    id: root
    spacing: Theme.spacingLg
    property bool reveal: false
    readonly property var models: [
        {id:"auto", label:qsTr("Auto")},
        {id:"luna", label:qsTr("GPT-6 Luna — fast and economical")},
        {id:"sol", label:qsTr("GPT-6 Sol — complex editing")},
        {id:"claude", label:"Claude Sonnet 5.5"},
        {id:"gemini", label:"Gemini 3.1 Flash Lite"},
        {id:"custom", label:qsTr("Custom model")}
    ]
    function modelIndex() {
        for (let i=0;i<models.length;++i) if (models[i].id === ReelsAI.modelChoice) return i
        return 0
    }
    ThemedLabel { text: "Polza.AI"; size: "base" }
    ThemedLabel { id: keyLabel; text: qsTr("API key") }
    ThemedTextField {
        id: keyField
        width: parent.width
        height: Theme.androidMinTouchTarget
        echoMode: root.reveal ? TextInput.Normal : TextInput.Password
        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
        placeholderText: ReelsAI.keySaved ? qsTr("Key saved — enter a new key to replace it") : "pza_…"
        Accessible.name: keyLabel.text
        enabled: !ReelsAI.busy && !ReelsAI.checkingConnection
    }
    Flow {
        width: parent.width
        spacing: Theme.androidTouchGap
        ThemedButton {
            height: Theme.androidMinTouchTarget
            text: root.reveal ? qsTr("Hide") : qsTr("Show")
            enabled: keyField.text.length > 0
            onClicked: root.reveal = !root.reveal
        }
        ThemedButton {
            height: Theme.androidMinTouchTarget
            text: qsTr("Save")
            enabled: keyField.text.trim().length > 0 && !ReelsAI.busy && !ReelsAI.checkingConnection
            onClicked: if (ReelsAI.saveKey(keyField.text)) { keyField.clear(); root.reveal = false }
        }
        ThemedButton {
            height: Theme.androidMinTouchTarget
            text: qsTr("Check connection")
            enabled: ReelsAI.keySaved && !ReelsAI.busy && !ReelsAI.checkingConnection
            onClicked: ReelsAI.checkConnection()
        }
        ThemedButton {
            height: Theme.androidMinTouchTarget
            text: qsTr("Delete API key")
            variant: "destructive"
            enabled: ReelsAI.keySaved || ReelsAI.checkingConnection
            onClicked: { ReelsAI.deleteKey(); keyField.clear(); root.reveal = false }
        }
    }
    ThemedLabel {
        width: parent.width
        wrapMode: Text.WordWrap
        text: ReelsAI.connectionStatus
        visible: text.length > 0
    }
    ThemedLabel {
        width: parent.width
        wrapMode: Text.WordWrap
        visible: !ReelsAI.persistentKey
        text: qsTr("On desktop the key is kept only for this session. Android uses Keystore.")
    }
    ThemedLabel { id: modelLabel; text: qsTr("AI model") }
    ThemedComboBox {
        width: parent.width
        height: Theme.androidMinTouchTarget
        model: root.models
        textRole: "label"
        valueRole: "id"
        currentIndex: root.modelIndex()
        enabled: !ReelsAI.busy && !ReelsAI.checkingConnection
        Accessible.name: modelLabel.text
        onActivated: ReelsAI.modelChoice = currentValue
    }
    ThemedTextField {
        width: parent.width
        height: Theme.androidMinTouchTarget
        visible: ReelsAI.modelChoice === "custom"
        enabled: !ReelsAI.busy && !ReelsAI.checkingConnection
        text: ReelsAI.customModel
        placeholderText: "provider/model-name"
        Accessible.name: qsTr("Custom model ID")
        onEditingFinished: ReelsAI.customModel = text
    }
    ThemedLabel {
        width: parent.width
        wrapMode: Text.WordWrap
        text: qsTr("Model availability, tools and image support are checked at runtime. Auto uses Luna; maximum quality uses Sol.")
    }
    ThemedLabel { text: qsTr("Maximum AI steps") }
    ThemedComboBox {
        width: parent.width
        height: Theme.androidMinTouchTarget
        model: [10,15,18,20,25,30]
        currentIndex: Math.max(0,model.indexOf(ReelsAI.maxAgentSteps))
        enabled: !ReelsAI.busy
        onActivated: ReelsAI.maxAgentSteps = model[currentIndex]
    }
    ThemedSwitch {
        width: parent.width
        text: qsTr("Developer logs")
        checked: ReelsAI.developerLogs
        onToggled: ReelsAI.developerLogs = checked
    }
    ThemedLabel {
        width: parent.width
        wrapMode: Text.WrapAnywhere
        visible: ReelsAI.developerLogs && ReelsAI.logs.length > 0
        text: ReelsAI.logs.join("\n")
        font.family: Theme.monoFontFamily
    }
    ThemedLabel {
        width: parent.width
        wrapMode: Text.WordWrap
        text: qsTr("Video editing runs on your device. When using AI, your prompt and selected frames/previews may be sent to the selected AI provider. Original videos are not uploaded automatically.")
    }
}
