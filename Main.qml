import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    width: 1200
    height: 900
    visible: true
    title: qsTr("Pipewire Configurator")

    property var configData: ({})
    property var quantumNames: ["default.clock.quantum", "default.clock.min-quantum", "default.clock.max-quantum", "default.clock.quantum-floor", "default.clock.quantum-limit"]

    Component.onCompleted: {
        loadAll();
    }

    function addResampleDefaultsToMap(map, prefix) {
        map[prefix + "resample.disable"] = "false";
        map[prefix + "resample.quality"] = "4";
        map[prefix + "resample.window"] = "kaiser";
        map[prefix + "resample.cutoff"] = "0.94";
        map[prefix + "resample.n-taps"] = "0";
        map[prefix + "resample.param.exp.A"] = "0.0";
        map[prefix + "resample.param.blackman.alpha"] = "0.0";
        map[prefix + "resample.param.kaiser.alpha"] = "0.0";
        map[prefix + "resample.param.kaiser.stopband-attenuation"] = "0.0";
        map[prefix + "resample.param.kaiser.transition-bandwidth"] = "0.0";
    }

    readonly property var systemDefaults: {
        let defaults = {};
        let pipewireConfContext = "pipewire.conf|context.properties|";
        let clientConfStream = "client.conf|stream.properties|";
        let pulseConfStream = "pipewire-pulse.conf|stream.properties|";
        let pulseConfPulse = "pipewire-pulse.conf|pulse.properties|";

        // Pipewire Main Server Defaults
        defaults[pipewireConfContext + "default.clock.rate"] = "48000";
        defaults[pipewireConfContext + "default.clock.allowed-rates"] = "[ 48000 ]";
        defaults[pipewireConfContext + "default.clock.quantum"] = "1024";
        defaults[pipewireConfContext + "default.clock.min-quantum"] = "32";
        defaults[pipewireConfContext + "default.clock.max-quantum"] = "2048";
        defaults[pipewireConfContext + "default.clock.quantum-limit"] = "8192";
        defaults[pipewireConfContext + "default.clock.quantum-floor"] = "4";

        addResampleDefaultsToMap(defaults, pipewireConfContext);
        addResampleDefaultsToMap(defaults, clientConfStream);
        addResampleDefaultsToMap(defaults, pulseConfStream);

        defaults[clientConfStream + "node.force-rate"] = "48000";
        defaults[pulseConfStream + "node.force-rate"] = "48000";

        defaults[pulseConfPulse + "pulse.min.req"] = "256/48000";
        defaults[pulseConfPulse + "pulse.default.req"] = "960/48000";
        defaults[pulseConfPulse + "pulse.min.frag"] = "256/48000";
        defaults[pulseConfPulse + "pulse.default.frag"] = "96000/48000";
        defaults[pulseConfPulse + "pulse.default.tlength"] = "96000/48000";
        defaults[pulseConfPulse + "pulse.min.quantum"] = "256/48000";

        return defaults;
    }

    function getSetting(key) {
        // 1. Check if the config file has a value
        if (configData[key] !== undefined) {
            return configData[key];
        }
        // 2. Fall back to our hardcoded default
        return systemDefaults[key] || "";
    }

    MessageDialog {
        id: resetWarning
        title: "Reset Configuration"
        text: "This will delete your custom settings and restart the audio engine. Proceed?"
        buttons: MessageDialog.Yes | MessageDialog.No
        onAccepted: {
            backend.resetToDefaults();
            loadConfigData();
            fillUI();
        }
    }

    MessageDialog {
        id: applyWarning
        title: "Apply Configuration"
        text: "This will apply your custom settings and restart the audio engine. Proceed?"
        buttons: MessageDialog.Yes | MessageDialog.No
        onAccepted: {
            saveAll();
            backend.restartServices();
        }
    }

    function loadAll() {
        let files = ["pipewire.conf", "pipewire-pulse.conf", "client.conf"];
        let fullData = {};

        files.forEach(file => {
            let settings = backend.loadSettings(file);
            for (let key in settings) {
                // Store in format: "pipewire.conf|context.properties|default.clock.rate"
                fullData[file + "|" + key] = settings[key];
            }
        });

        configData = fullData;
        fillUI();
    }

    function collectPulseBridgeSettings(map) {
        let section = "pulse.properties|";
        let settings = ["pulse.min.req", "pulse.default.req", "pulse.min.frag", "pulse.default.frag", "pulse.default.tlength", "pulse.min.quantum"];

        settings.forEach(name => {
            let numField = findChildByObjectName("pulse_num_" + name);
            let denField = findChildByObjectName("pulse_den_" + name);

            if (numField && denField) {
                let num = numField.text || "0";
                let den = denField.text || "1";
                // Formats as "2048/192000"
                map[section + name] = num + "/" + den;
            }
        });
    }

    function setComboByText(combo, val) {
        if (!val)
            return;
        let idx = combo.find(val.toString());
        if (idx !== -1)
            combo.currentIndex = idx;
    }

    function collectResamplerSettings(map, section) {
        map[section + "resample.disable"] = resampleEnabled.checked ? "false" : "true";
        map[section + "resample.quality"] = resampleQualityCombo.currentText || "4";
        map[section + "resample.cutoff"] = cutoffFreqCombo.currentText || "0.91";
        map[section + "resample.prefill"] = prefillCheck.checked ? "true" : "false";
        map[section + "resample.window"] = windowTypeSelector.currentText.toLowerCase() || "kaiser";

        if (windowTypeSelector.currentText === "Kaiser") {
            map[section + "resample.param.kaiser.alpha"] = kaiserAlpha.text || "0.0";
            map[section + "resample.param.kaiser.stopband-attenuation"] = kaiserStopband.text || "0.0";
            map[section + "resample.param.kaiser.transition-bandwidth"] = kaiserTransitionBandwidth.text || "0.0";
        } else if (windowTypeSelector.currentText === "Exp") {
            map[section + "resample.param.exp.A"] = expA.text || "0.0";
        } else if (windowTypeSelector.currentText === "Blackman") {
            map[section + "resample.param.blackman.alpha"] = blackmanAlpha.text || "0.0";
        }
    }

    function collectChannelmixSettings(map, section) {
        map[section + "channelmix.disable"] = "false";
        map[section + "channelmix.normalize"] = "false";
        map[section + "channelmix.mix-lfe"] = "false";
        map[section + "channelmix.lock-volume"] = "false";
        map[section + "channelmix.upmix"] = "false";
        map[section + "channelmix.upmix-method"] = "none";
        map[section + "channelmix.lfe-cutoff"] = "0";
        map[section + "channelmix.fc-cutoff"] = "0";
        map[section + "channelmix.rear-delay"] = "0";
        map[section + "channelmix.stereo-widen"] = "0.0";
        map[section + "channelmix.hilbert-taps"] = "0";
        map[section + "dither.method"] = "none";
        map[section + "dither.noise"] = "0";
        map[section + "audioconvert.filter-graph.disable"] = "true";
    }

    function fillUI() {
        if (!configData)
            return;

        // --- 1. Pipewire.conf (Main Server) ---
        let p = "pipewire.conf|context.properties|";
        let pulse = "pipewire-pulse.conf|stream.properties|";

        setComboByText(defaultClockRate, getSetting(p + "default.clock.rate"));
        setComboByText(globalForceRate, getSetting(pulse + "node.force-rate"));

        // Allowed Rates (Special Array Handling: "[ 44100 48000 ]")
        let allowed = getSetting(p + "default.clock.allowed-rates");
        if (allowed) {
            let rates = allowed.replace(/[\[\]]/g, "").trim().split(/\s+/);
            for (let i = 0; i < rateModel.count; i++) {
                rateModel.setProperty(i, "selected", rates.indexOf(rateModel.get(i).rate.toString()) !== -1);
            }
        }

        // Resampler Settings
        resampleEnabled.checked = !(getSetting(p + "resample.disable") === "true");
        setComboByText(resampleQualityCombo, getSetting(p + "resample.quality"));
        setComboByText(cutoffFreqCombo, getSetting(p + "resample.cutoff"));
        prefillCheck.checked = (getSetting(p + "resample.prefill") === "true");
        setComboByText(windowTypeSelector, getSetting(p + "resample.window"));

        // Window Parameters
        kaiserAlpha.text = getSetting(p + "resample.param.kaiser.alpha") || "";
        kaiserStopband.text = getSetting(p + "resample.param.kaiser.stopband-attenuation") || "";
        kaiserTransitionBandwidth.text = getSetting(p + "resample.param.kaiser.transition-bandwidth") || "";

        // Extra Fields
        resampleCutoffText.text = getSetting(p + "resample.cutoff") || "";
        nTapsText.text = getSetting(p + "resample.n-taps") || "";

        let qNames = ["default.clock.quantum", "default.clock.min-quantum", "default.clock.max-quantum", "default.clock.quantum-floor", "default.clock.quantum-limit"];

        qNames.forEach(qKey => {
            let val = getSetting("pipewire.conf|context.properties|" + qKey);
            if (val) {
                let combo = findChildByObjectName("quantum_" + qKey);
                if (combo) {
                    setComboByText(combo, val);
                }
            }
        });

        // --- 2. PulseAudio Bridge ---
        // Note: Logic for the Repeater fields would go here based on configData["pipewire-pulse.conf|..."]

        let pulseSection = "pipewire-pulse.conf|pulse.properties|";
        let pulseSettings = ["pulse.min.req", "pulse.default.req", "pulse.min.frag", "pulse.default.frag", "pulse.default.tlength", "pulse.min.quantum"];

        pulseSettings.forEach(name => {
            let val = getSetting(pulseSection + name);
            if (val) {
                // Split "2048/192000" into ["2048", "192000"]
                let parts = val.split('/');

                let numField = findChildByObjectName("pulse_num_" + name);
                let denField = findChildByObjectName("pulse_den_" + name);

                if (numField && parts[0]) {
                    numField.text = parts[0];
                }
                if (denField && parts[1]) {
                    denField.text = parts[1];
                }
            }
        });
    }

    // Helper function to find those repeated ComboBoxes
    function findChildByObjectName(name) {
        return findChild(window.contentItem, name);
    }

    function findChild(parent, name) {
        for (let i = 0; i < parent.children.length; i++) {
            if (parent.children[i].objectName === name)
                return parent.children[i];
            let found = findChild(parent.children[i], name);
            if (found)
                return found;
        }
        return null;
    }

    function saveAll() {
        console.log("Syncing GUI to Pipewire Config Files...");

        // --- Build pipewire.conf Map ---
        let pwMap = {};
        let contextProps = "context.properties|";
        let streamingProps = "stream.properties|";
        let pulseProps = "pulse.properties|";

        pwMap[contextProps + "default.clock.rate"] = defaultClockRate.currentText;

        // Convert selected rates back to Pipewire array format: [ 44100 48000 ]
        let selectedRates = [];
        for (let i = 0; i < rateModel.count; i++) {
            if (rateModel.get(i).selected)
                selectedRates.push(rateModel.get(i).rate);
        }
        pwMap[contextProps + "default.clock.allowed-rates"] = "[ " + selectedRates.join(" ") + " ]";

        for (let it = 0; it < quantumNames.length; it++) {
            let key = quantumNames[it];
            // We look for the object we named "quantum_default.clock.quantum" etc.
            // This is a common way to get data out of a Repeater
            pwMap[contextProps + key] = findChildByObjectName("quantum_" + key).currentText;
        }

        collectChannelmixSettings(pwMap, contextProps);
        collectResamplerSettings(pwMap, contextProps);

        backend.saveToUserConfig("pipewire.conf", pwMap);

        let pulseMap = {};
        collectChannelmixSettings(pulseMap, streamingProps);
        collectResamplerSettings(pulseMap, streamingProps);
        collectPulseBridgeSettings(pulseMap);
        pulseMap[pulseProps + "pulse.default.format"] = "F32";
        pulseMap[streamingProps + "node.force-rate"] = globalForceRate.currentText;

        backend.saveToUserConfig("pipewire-pulse.conf", pulseMap);

        // --- Build client.conf Map ---
        let clientMap = {};
        collectChannelmixSettings(clientMap, streamingProps);
        collectResamplerSettings(clientMap, streamingProps);
        clientMap[streamingProps + "node.force-rate"] = globalForceRate.currentText;

        backend.saveToUserConfig("client.conf", clientMap);

        console.log("All configurations saved to ~/.config/pipewire/");
    }

    // --- Header with Reset Button ---
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.rightMargin: 10
            Label {
                text: "Pipewire Settings"
                font.bold: true
                Layout.leftMargin: 10
            }
            Item {
                Layout.fillWidth: true
            } // Spacer
            Button {
                text: "Reset and apply defaults"
                onClicked: {
                    resetWarning.open();
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: bar
            Layout.fillWidth: true
            TabButton {
                text: qsTr("General Settings")
            } // New Page 1
            TabButton {
                text: qsTr("Pipewire Server")
            }
            TabButton {
                text: qsTr("PulseAudio Client")
            }
        }

        StackLayout {
            id: layout
            currentIndex: bar.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 20

            // --- PAGE 1: Resampler (formerly General Settings) ---
            ScrollView {
                id: generalScroll
                clip: true
                Layout.fillWidth: true
                Layout.fillHeight: true

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    parent: generalScroll
                    anchors.top: generalScroll.top
                    anchors.bottom: generalScroll.bottom
                    anchors.right: generalScroll.right
                }
                // THIS IS THE ONE AND ONLY DIRECT CHILD
                ColumnLayout {
                    width: generalScroll.availableWidth
                    spacing: 20 // Added a bit more space between sections
                    Layout.margins: 15
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            text: "Global Node Configuration"
                            font.bold: true
                            font.pointSize: 14
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: "node.force-rate:"
                                font.family: "Monospace"
                                Layout.preferredWidth: 200
                            }
                            ComboBox {
                                id: globalForceRate
                                Layout.fillWidth: true
                                // References your master rateModel from the Server tab
                                model: rateModel
                                textRole: "rate"
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: "#444444"
                    }

                    ColumnLayout {
                        width: generalScroll.availableWidth
                        spacing: 15

                        Label {
                            text: "Resampler Configuration"
                            font.bold: true
                            font.pointSize: 14
                        }

                        // 1. The Master Toggle
                        CheckBox {
                            id: resampleEnabled
                            text: "Enable Resampling"
                            checked: true
                        }

                        // --- Everything below this is dependent on the CheckBox above ---
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 15
                            enabled: resampleEnabled.checked // Greys out and disables interaction
                            opacity: enabled ? 1.0 : 0.5     // Visually dims the section

                            // Resample Quality (1-14)
                            RowLayout {
                                Label {
                                    text: "Resample Quality:"
                                    Layout.preferredWidth: 200
                                }
                                ComboBox {
                                    id: resampleQualityCombo
                                    model: Array.from({
                                        length: 14
                                    }, (_, i) => i + 1)
                                    Layout.fillWidth: true
                                }
                            }

                            // Cutoff Frequency (0.01 - 1.00)
                            RowLayout {
                                Label {
                                    text: "Cutoff Freq:"
                                    Layout.preferredWidth: 200
                                }
                                ComboBox {
                                    id: cutoffFreqCombo
                                    // Generates 0.01 to 1.00
                                    model: Array.from({
                                        length: 100
                                    }, (_, i) => ((i + 1) / 100).toFixed(2))
                                    Layout.fillWidth: true
                                }
                            }

                            CheckBox {
                                id: prefillCheck
                                text: "Prefill buffer with 0s"
                                Layout.fillWidth: true
                            }

                            // Resample Window Selection
                            RowLayout {
                                Label {
                                    text: "Resample Window:"
                                    Layout.preferredWidth: 200
                                }
                                ComboBox {
                                    id: windowTypeSelector
                                    model: ["Kaiser", "Exp", "Blackman"]
                                    Layout.fillWidth: true
                                }
                            }

                            // Configure Button (Opens Popup)
                            Button {
                                text: "Configure " + windowTypeSelector.currentText + "..."
                                Layout.alignment: Qt.AlignRight
                                onClicked: configPopup.open()
                            }

                            // Extra Configurable Doubles
                            RowLayout {
                                Label {
                                    text: "Resample Cutoff:"
                                    Layout.preferredWidth: 200
                                }
                                TextField {
                                    id: resampleCutoffText
                                    placeholderText: "0.0"
                                    validator: DoubleValidator {}
                                    Layout.fillWidth: true
                                }
                            }

                            RowLayout {
                                Label {
                                    text: "N-Taps:"
                                    Layout.preferredWidth: 200
                                }
                                TextField {
                                    id: nTapsText
                                    placeholderText: "0.0"
                                    validator: DoubleValidator {}
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
                // --- Configuration Popup ---
                Popup {
                    id: configPopup
                    parent: Overlay.overlay
                    x: Math.round((parent.width - width) / 2)
                    y: Math.round((parent.height - height) / 2)
                    width: 350
                    modal: true
                    focus: true

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        Label {
                            text: windowTypeSelector.currentText + " Parameters"
                            font.bold: true
                        }

                        // Dynamic Fields based on ComboBox selection
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            // CASE: Exp
                            ColumnLayout {
                                visible: windowTypeSelector.currentText === "Exp"
                                Layout.fillWidth: true
                                RowLayout {
                                    Label {
                                        text: "Option A:"
                                        Layout.preferredWidth: 100
                                    }
                                    TextField {
                                        id: expA
                                        Layout.fillWidth: true

                                        // Setting decimals: 2 ensures they can only type two decimal places
                                        validator: DoubleValidator {
                                            bottom: 0.0
                                            decimals: 2
                                            notation: DoubleValidator.StandardNotation
                                        }

                                        placeholderText: "0.00"
                                    }
                                }
                            }

                            // CASE: Kaiser
                            ColumnLayout {
                                visible: windowTypeSelector.currentText === "Kaiser"
                                Layout.fillWidth: true
                                RowLayout {
                                    Label {
                                        text: "Alpha:"
                                        Layout.preferredWidth: 150
                                    }
                                    TextField {
                                        id: kaiserAlpha
                                        Layout.fillWidth: true

                                        // Setting decimals: 2 ensures they can only type two decimal places
                                        validator: DoubleValidator {
                                            bottom: 0.0
                                            decimals: 2
                                            notation: DoubleValidator.StandardNotation
                                        }

                                        placeholderText: "0.00"
                                    }
                                }
                                RowLayout {
                                    Label {
                                        text: "Stopband Attenuation:"
                                        Layout.preferredWidth: 150
                                    }
                                    TextField {
                                        id: kaiserStopband
                                        Layout.fillWidth: true

                                        // Setting decimals: 2 ensures they can only type two decimal places
                                        validator: DoubleValidator {
                                            bottom: 0.0
                                            decimals: 2
                                            notation: DoubleValidator.StandardNotation
                                        }

                                        placeholderText: "0.00"
                                    }
                                }
                                RowLayout {
                                    Label {
                                        text: "Trans. Bandwidth:"
                                        Layout.preferredWidth: 150
                                    }
                                    TextField {
                                        id: kaiserTransitionBandwidth
                                        Layout.fillWidth: true

                                        // Setting decimals: 2 ensures they can only type two decimal places
                                        validator: DoubleValidator {
                                            bottom: 0.0
                                            decimals: 2
                                            notation: DoubleValidator.StandardNotation
                                        }

                                        placeholderText: "0.00"
                                    }
                                }
                            }

                            // CASE: Blackman
                            ColumnLayout {
                                visible: windowTypeSelector.currentText === "Blackman"
                                Layout.fillWidth: true
                                RowLayout {
                                    Label {
                                        text: "Alpha:"
                                        Layout.preferredWidth: 100
                                    }
                                    TextField {
                                        id: blackmanAlpha
                                        Layout.fillWidth: true

                                        // Setting decimals: 2 ensures they can only type two decimal places
                                        validator: DoubleValidator {
                                            bottom: 0.0
                                            decimals: 2
                                            notation: DoubleValidator.StandardNotation
                                        }

                                        placeholderText: "0.00"
                                    }
                                }
                            }
                        }

                        Button {
                            text: "Done"
                            onClicked: configPopup.close()
                            Layout.alignment: Qt.AlignRight
                        }
                    }
                }
            }

            // 1. Pipewire Server Tab
            ScrollView {
                clip: true
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    width: parent.width - 40
                    spacing: 15

                    Label {
                        text: "Pipewire Server Configuration"
                        font.pixelSize: 18
                        font.bold: true
                    }

                    // Default Clock Rate
                    RowLayout {
                        Label {
                            text: "Default Clock Rate:"
                            Layout.preferredWidth: 200
                        }
                        ComboBox {
                            id: defaultClockRate
                            model: [44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000, 705600, 768000, 1411200, 1536000]
                            Layout.fillWidth: true
                        }
                    }

                    // Allowed Clock Rates (Multi-select simulation)
                    // Note: Standard ComboBox is single-select. For multi-select,
                    // we usually use a customized popup with CheckBoxes.
                    // The Display Row
                    RowLayout {
                        Layout.fillWidth: true // Added this here too

                        Label {
                            text: "Allowed Clock Rates:"
                            Layout.preferredWidth: 200
                        }

                        Button {
                            text: getSelectedRates()
                            Layout.fillWidth: true // Now this has "room" to grow
                            onClicked: ratePopup.open()
                        }
                    }

                    // Quantum Settings Group
                    Label {
                        text: "Quantum Settings"
                        font.bold: true
                        Layout.topMargin: 10
                    }
                    Label {
                        text: "These are the settings for the default clock rate and scale with it"
                        font.bold: false
                        Layout.topMargin: 10
                    }

                    Repeater {
                        model: quantumNames
                        delegate: RowLayout {
                            Label {
                                text: modelData + ":"
                                Layout.preferredWidth: 200
                            }
                            ComboBox {
                                id: qBox
                                objectName: "quantum_" + modelData // Critical for finding it later
                                model: [4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536]
                                Layout.fillWidth: true

                                // To help fillUI find this box later:
                                Component.onCompleted: {
                                    if (configData["pipewire.conf|context.properties|" + modelData]) {
                                        let val = configData["pipewire.conf|context.properties|" + modelData];
                                        currentIndex = find(val.toString());
                                    }
                                }
                            }
                            Label {
                                // Calculate: (Quantum / SampleRate) * 1000
                                // We use parseFloat to ensure math works even if values are strings
                                readonly property double ms: (parseFloat(qBox.currentText) / parseFloat(defaultClockRate.currentText)) * 1000

                                text: ms.toFixed(2) + " ms"
                                color: "#888888" // Light grey
                                font.pixelSize: 11
                                Layout.preferredWidth: 60
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }
                }
            }

            // --- PAGE 4: PulseAudio Client ---
            ScrollView {
                id: pulseScroll
                clip: true
                Layout.fillWidth: true
                Layout.fillHeight: true

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    parent: pulseScroll
                    anchors.top: pulseScroll.top
                    anchors.bottom: pulseScroll.bottom
                    anchors.right: pulseScroll.right
                }

                ColumnLayout {
                    width: pulseScroll.availableWidth
                    spacing: 20
                    Layout.margins: 15

                    Label {
                        text: "PulseAudio Bridge Configuration"
                        font.bold: true
                        font.pointSize: 14
                    }

                    // We use a Repeater to generate the 6 similar rows to keep code clean
                    Repeater {
                        model: [
                            {
                                name: "pulse.min.req",
                                num: "2048",
                                den: "192000"
                            },
                            {
                                name: "pulse.default.req",
                                num: "4096",
                                den: "192000"
                            },
                            {
                                name: "pulse.min.frag",
                                num: "2048",
                                den: "192000"
                            },
                            {
                                name: "pulse.default.frag",
                                num: "384000",
                                den: "192000"
                            },
                            {
                                name: "pulse.default.tlength",
                                num: "384000",
                                den: "192000"
                            },
                            {
                                name: "pulse.min.quantum",
                                num: "2048",
                                den: "192000"
                            }
                        ]

                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5

                            Label {
                                text: modelData.name
                                font.family: "Monospace"
                                color: "#3498db"
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                TextField {
                                    // Unique name for numerator
                                    objectName: "pulse_num_" + modelData.name
                                    placeholderText: "Numerator"
                                    text: modelData.num
                                    validator: IntValidator {
                                        bottom: 1
                                    }
                                    Layout.fillWidth: true
                                }

                                Label {
                                    text: "/"
                                    font.bold: true
                                }

                                TextField {
                                    // Unique name for denominator
                                    objectName: "pulse_den_" + modelData.name
                                    placeholderText: "Sample Rate"
                                    text: modelData.den
                                    validator: IntValidator {
                                        bottom: 1
                                    }
                                    Layout.fillWidth: true
                                }

                                Label {
                                    // Updated to use the objectName references for live calculation
                                    property real n: parseInt(parent.children[0].text) || 0
                                    property real d: parseInt(parent.children[2].text) || 1
                                    text: "(" + ((n / d) * 1000).toFixed(2) + " ms)"
                                    color: "gray"
                                    Layout.preferredWidth: 80
                                }
                            }

                            // Horizontal line separator
                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: "#eeeeee"
                                Layout.topMargin: 5
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            id: bottomActions
            Layout.fillWidth: true
            Layout.margins: 10
            Layout.topMargin: 0 // Keep it tight to the content above

            // Spacer to push buttons to the right
            Item {
                Layout.fillWidth: true
            }

            Button {
                text: "Save & Apply"
                onClicked: {
                    applyWarning.open();
                }
            }

            Button {
                text: "Save"
                highlighted: true // Makes it stand out (usually blue/primary color)
                onClicked: {
                    saveAll();
                }
            }
        }
    }

    ListModel {
        id: rateModel
        ListElement {
            rate: 44100
            selected: false
        }
        ListElement {
            rate: 48000
            selected: true
        }
        ListElement {
            rate: 88200
            selected: false
        }
        ListElement {
            rate: 96000
            selected: false
        }
        ListElement {
            rate: 176400
            selected: false
        }
        ListElement {
            rate: 192000
            selected: false
        }
        ListElement {
            rate: 352800
            selected: false
        }
        ListElement {
            rate: 384000
            selected: false
        }
        ListElement {
            rate: 705600
            selected: false
        }
        ListElement {
            rate: 768000
            selected: false
        }
        ListElement {
            rate: 1411200
            selected: false
        }
        ListElement {
            rate: 1536000
            selected: false
        }
    }

    function getSelectedRates() {
        let selected = [];
        for (let i = 0; i < rateModel.count; i++) {
            if (rateModel.get(i).selected) {
                selected.push(rateModel.get(i).rate);
            }
        }
        return selected.length > 0 ? selected.join(", ") : "None selected";
    }

    // The Updated Popup
    Popup {
        id: ratePopup
        // ... same positioning as before ...

        ColumnLayout {
            anchors.fill: parent
            Label {
                text: "Select Allowed Rates"
                font.bold: true
            }

            Repeater {
                model: rateModel
                delegate: CheckBox {
                    text: model.rate
                    checked: model.selected
                    onToggled: {
                        // Update the model when clicked
                        rateModel.setProperty(index, "selected", checked);
                    }
                }
            }

            Button {
                text: "Done"
                onClicked: ratePopup.close()
                Layout.alignment: Qt.AlignRight
            }
        }
    }
}
