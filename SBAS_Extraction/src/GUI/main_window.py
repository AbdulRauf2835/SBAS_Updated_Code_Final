"""
main_window.py
--------------
Presentation Layer: Main window for Hercules Transmitter GUI.
Matches the functionality of the console menu system.
"""

from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QPushButton,
    QLabel, QTextEdit, QGroupBox, QSpinBox, QComboBox, QLineEdit,
    QFileDialog, QMessageBox, QStatusBar, QTabWidget
)
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QFont, QAction
from field_editor import FieldEditorWidget


class MainWindow(QMainWindow):
    """
    Main window for Hercules Transmitter GUI
    
    Provides UI matching all console menu options:
    1. Start transmission (with duration timer)
    2. Stop transmission
    3. Show client status
    4. Edit and send a message
    5. Set Orbit Model
    6. Edit and transmit SINGLE message continuously
    7. Configure RINEX Output
    0. Exit
    """
    
    def __init__(self, controller):
        super().__init__()
        self.controller = controller
        self.setWindowTitle("Hercules SBAS Transmitter System")
        self.setGeometry(100, 100, 1000, 700)
        
        self._setup_ui()
        self._setup_menu_bar()
        self._setup_status_bar()
        self._connect_signals()
        
        # Timer for updating status
        self.status_timer = QTimer()
        self.status_timer.timeout.connect(self._update_status)
        self.status_timer.start(1000)  # Update every second
        
        # Transmission time tracking
        self.transmission_start_time = None
        self.transmission_duration = 0  # in seconds, 0 = continuous
    
    def _setup_ui(self):
        """Setup the main UI layout"""
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        main_layout = QVBoxLayout(central_widget)
        
        # Title
        title = QLabel("HERCULES TRANSMITTER SYSTEM")
        title_font = QFont()
        title_font.setPointSize(16)
        title_font.setBold(True)
        title.setFont(title_font)
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        main_layout.addWidget(title)
        
        # Tab widget for different sections
        tabs = QTabWidget()
        
        # Tab 1: Transmission Control
        tabs.addTab(self._create_transmission_tab(), "Transmission")
        
        # Tab 2: Message Editor
        tabs.addTab(self._create_message_editor_tab(), "Message Editor")
        
        # Tab 3: Configuration
        tabs.addTab(self._create_config_tab(), "Configuration")
        
        main_layout.addWidget(tabs)
        
        # Log display (shared across all tabs)
        log_group = QGroupBox("System Log")
        log_layout = QVBoxLayout()
        
        self.log_display = QTextEdit()
        self.log_display.setReadOnly(True)
        self.log_display.setMaximumHeight(200)
        log_layout.addWidget(self.log_display)
        
        log_group.setLayout(log_layout)
        main_layout.addWidget(log_group)
    
    def _create_transmission_tab(self):
        """Create the transmission control tab (Menu options 1, 2, 3)"""
        widget = QWidget()
        layout = QVBoxLayout(widget)
        
        # File Selection
        file_group = QGroupBox("Data Source")
        file_layout = QHBoxLayout()
        
        self.file_path_edit = QLineEdit()
        self.file_path_edit.setPlaceholderText("Select RINEX .25B/.26B file...")
        file_layout.addWidget(QLabel("Input File:"))
        file_layout.addWidget(self.file_path_edit)
        
        self.browse_btn = QPushButton("Browse...")
        self.browse_btn.clicked.connect(self._browse_file)
        file_layout.addWidget(self.browse_btn)
        
        self.bind_btn = QPushButton("Load File")
        self.bind_btn.clicked.connect(self._bind_file)
        file_layout.addWidget(self.bind_btn)
        
        file_group.setLayout(file_layout)
        layout.addWidget(file_group)
        
        # Transmission Settings
        settings_group = QGroupBox("Transmission Settings")
        settings_layout = QVBoxLayout()
        
        # Satellite Filter
        sat_layout = QHBoxLayout()
        sat_layout.addWidget(QLabel("Satellite Filter:"))
        self.sat_filter_spin = QSpinBox()
        self.sat_filter_spin.setRange(0, 255)
        self.sat_filter_spin.setValue(0)
        self.sat_filter_spin.setSpecialValueText("All Satellites")
        sat_layout.addWidget(self.sat_filter_spin)
        sat_layout.addStretch()
        settings_layout.addLayout(sat_layout)
        
        # Fast Correction Satellites
        fc_layout = QHBoxLayout()
        fc_layout.addWidget(QLabel("Fast Correction Satellites:"))
        self.fc_sats_spin = QSpinBox()
        self.fc_sats_spin.setRange(0, 51)
        self.fc_sats_spin.setValue(0)
        self.fc_sats_spin.setSpecialValueText("All MT2-5")
        fc_layout.addWidget(self.fc_sats_spin)
        fc_layout.addStretch()
        settings_layout.addLayout(fc_layout)
        
        # Duration
        dur_layout = QHBoxLayout()
        dur_layout.addWidget(QLabel("Duration (seconds):"))
        self.duration_spin = QSpinBox()
        self.duration_spin.setRange(0, 86400)  # 0 to 24 hours
        self.duration_spin.setValue(0)
        self.duration_spin.setSpecialValueText("Continuous")
        dur_layout.addWidget(self.duration_spin)
        dur_layout.addStretch()
        settings_layout.addLayout(dur_layout)
        
        settings_group.setLayout(settings_layout)
        layout.addWidget(settings_group)
        
        # Control Buttons
        control_layout = QHBoxLayout()
        
        self.start_btn = QPushButton("Start Transmission")
        self.start_btn.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;")
        self.start_btn.clicked.connect(self._start_transmission)
        control_layout.addWidget(self.start_btn)
        
        self.stop_btn = QPushButton("Stop Transmission")
        self.stop_btn.setStyleSheet("background-color: #f44336; color: white; font-weight: bold;")
        self.stop_btn.setEnabled(False)
        self.stop_btn.clicked.connect(self._stop_transmission)
        control_layout.addWidget(self.stop_btn)
        
        layout.addLayout(control_layout)
        
        # Status Display
        status_group = QGroupBox("Status")
        status_layout = QVBoxLayout()
        
        self.status_label = QLabel("Status: Idle")
        self.status_label.setStyleSheet("font-weight: bold;")
        status_layout.addWidget(self.status_label)
        
        self.clients_label = QLabel("Connected Clients: 0")
        status_layout.addWidget(self.clients_label)
        
        self.messages_label = QLabel("Loaded Messages: 0")
        status_layout.addWidget(self.messages_label)
        
        # Transmission time tracking
        self.time_elapsed_label = QLabel("Time Elapsed: --:--:--")
        self.time_elapsed_label.setStyleSheet("color: #2196F3; font-weight: bold;")
        status_layout.addWidget(self.time_elapsed_label)
        
        self.time_remaining_label = QLabel("Time Remaining: --:--:--")
        self.time_remaining_label.setStyleSheet("color: #FF9800; font-weight: bold;")
        status_layout.addWidget(self.time_remaining_label)
        
        status_group.setLayout(status_layout)
        layout.addWidget(status_group)
        
        layout.addStretch()
        return widget
    
    def _create_message_editor_tab(self):
        """Create message editor tab (Menu options 4, 6)"""
        widget = QWidget()
        layout = QVBoxLayout(widget)
        
        # Message Selection
        select_group = QGroupBox("Select Message")
        select_layout = QHBoxLayout()
        
        select_layout.addWidget(QLabel("Satellite ID:"))
        self.msg_sat_spin = QSpinBox()
        self.msg_sat_spin.setRange(1, 255)
        self.msg_sat_spin.setValue(128)
        select_layout.addWidget(self.msg_sat_spin)
        
        select_layout.addWidget(QLabel("Message Type:"))
        self.msg_type_combo = QComboBox()
        self.msg_type_combo.addItems([
            "MT1 - PRN Mask", "MT2 - Fast Corrections", "MT3 - Fast Corrections",
            "MT4 - Fast Corrections", "MT5 - Fast Corrections", "MT6 - Integrity",
            "MT7 - Degradation", "MT9 - GEO Navigation", "MT10 - Degradation",
            "MT17 - GEO Almanac", "MT18 - IGP Mask", "MT24 - Mixed Corrections",
            "MT25 - Long-Term Corrections", "MT26 - Ionospheric", "MT28 - Covariance"
        ])
        select_layout.addWidget(self.msg_type_combo)
        
        self.load_msg_btn = QPushButton("Load Message")
        self.load_msg_btn.clicked.connect(self._load_message)
        select_layout.addWidget(self.load_msg_btn)
        
        select_group.setLayout(select_layout)
        layout.addWidget(select_group)
        
        # Message Data Editor (using FieldEditorWidget)
        editor_group = QGroupBox("Message Fields")
        editor_layout = QVBoxLayout()
        
        self.field_editor = FieldEditorWidget(self.controller)
        editor_layout.addWidget(self.field_editor)
        
        editor_group.setLayout(editor_layout)
        layout.addWidget(editor_group)
        
        # Actions
        actions_layout = QHBoxLayout()
        
        self.send_once_btn = QPushButton("Send Once")
        self.send_once_btn.clicked.connect(self._send_message_once)
        actions_layout.addWidget(self.send_once_btn)
        
        self.start_continuous_btn = QPushButton("Start Continuous")
        self.start_continuous_btn.clicked.connect(self._start_continuous_tx)
        actions_layout.addWidget(self.start_continuous_btn)
        
        self.stop_continuous_btn = QPushButton("Stop Continuous")
        self.stop_continuous_btn.setEnabled(False)
        self.stop_continuous_btn.clicked.connect(self._stop_continuous_tx)
        actions_layout.addWidget(self.stop_continuous_btn)
        
        layout.addLayout(actions_layout)
        
        return widget
    
    def _create_config_tab(self):
        """Create configuration tab (Menu options 5, 7)"""
        widget = QWidget()
        layout = QVBoxLayout(widget)
        
        # Orbit Model Configuration
        orbit_group = QGroupBox("Orbit Model Configuration")
        orbit_layout = QVBoxLayout()
        
        # Model Selection
        model_layout = QHBoxLayout()
        model_layout.addWidget(QLabel("Orbit Model:"))
        self.orbit_model_combo = QComboBox()
        self.orbit_model_combo.addItems([
            "None (Use File Data)",
            "TLE (Two-Line Elements)",
            "Statistical - Satellite 128",
            "Statistical - Satellite 143"
        ])
        self.orbit_model_combo.currentIndexChanged.connect(self._orbit_model_changed)
        model_layout.addWidget(self.orbit_model_combo)
        model_layout.addStretch()
        orbit_layout.addLayout(model_layout)
        
        # TLE File Selection (only visible when TLE selected)
        self.tle_file_widget = QWidget()
        tle_layout = QHBoxLayout(self.tle_file_widget)
        tle_layout.setContentsMargins(0, 0, 0, 0)
        
        tle_layout.addWidget(QLabel("TLE File:"))
        self.tle_path_edit = QLineEdit()
        tle_layout.addWidget(self.tle_path_edit)
        
        self.tle_browse_btn = QPushButton("Browse...")
        self.tle_browse_btn.clicked.connect(self._browse_tle_file)
        tle_layout.addWidget(self.tle_browse_btn)
        
        orbit_layout.addWidget(self.tle_file_widget)
        self.tle_file_widget.setVisible(False)
        
        # Apply Orbit Model Button
        self.apply_orbit_btn = QPushButton("Apply Orbit Model")
        self.apply_orbit_btn.clicked.connect(self._apply_orbit_model)
        orbit_layout.addWidget(self.apply_orbit_btn)
        
        orbit_group.setLayout(orbit_layout)
        layout.addWidget(orbit_group)
        
        # RINEX Output Configuration
        rinex_group = QGroupBox("RINEX Output Configuration")
        rinex_layout = QVBoxLayout()
        
        rinex_file_layout = QHBoxLayout()
        rinex_file_layout.addWidget(QLabel("Output File:"))
        self.rinex_path_edit = QLineEdit()
        rinex_file_layout.addWidget(self.rinex_path_edit)
        
        self.rinex_browse_btn = QPushButton("Browse...")
        self.rinex_browse_btn.clicked.connect(self._browse_rinex_output)
        rinex_file_layout.addWidget(self.rinex_browse_btn)
        
        rinex_layout.addLayout(rinex_file_layout)
        
        rinex_actions = QHBoxLayout()
        self.enable_rinex_btn = QPushButton("Enable RINEX Output")
        self.enable_rinex_btn.clicked.connect(self._enable_rinex_output)
        rinex_actions.addWidget(self.enable_rinex_btn)
        
        self.disable_rinex_btn = QPushButton("Disable RINEX Output")
        self.disable_rinex_btn.clicked.connect(self._disable_rinex_output)
        rinex_actions.addWidget(self.disable_rinex_btn)
        
        rinex_layout.addLayout(rinex_actions)
        
        rinex_group.setLayout(rinex_layout)
        layout.addWidget(rinex_group)
        
        layout.addStretch()
        return widget
    
    def _setup_menu_bar(self):
        """Setup the menu bar"""
        menubar = self.menuBar()
        
        # File Menu
        file_menu = menubar.addMenu("&File")
        
        open_action = QAction("&Open RINEX File...", self)
        open_action.setShortcut("Ctrl+O")
        open_action.triggered.connect(self._browse_file)
        file_menu.addAction(open_action)
        
        file_menu.addSeparator()
        
        exit_action = QAction("E&xit", self)
        exit_action.setShortcut("Ctrl+Q")
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)
        
        # Help Menu
        help_menu = menubar.addMenu("&Help")
        
        about_action = QAction("&About", self)
        about_action.triggered.connect(self._show_about)
        help_menu.addAction(about_action)
    
    def _setup_status_bar(self):
        """Setup the status bar"""
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        self.status_bar.showMessage("Ready")
    
    def _connect_signals(self):
        """Connect controller signals"""
        # Will be connected by controller
        pass
    
    # Slot methods
    
    def _browse_file(self):
        """Browse for RINEX .25B/.26B file"""
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "Select RINEX File",
            "",
            "RINEX Files (*.25B *.26B *.txt);;All Files (*.*)"
        )
        if file_path:
            self.file_path_edit.setText(file_path)
    
    def _bind_file(self):
        """Bind the selected file"""
        file_path = self.file_path_edit.text()
        if not file_path:
            QMessageBox.warning(self, "No File", "Please select a file first.")
            return
        
        self.controller.bind_file(file_path)
    
    def _start_transmission(self):
        """Start transmission"""
        sat_filter = self.sat_filter_spin.value()
        fc_sats = self.fc_sats_spin.value()
        duration = self.duration_spin.value()
        
        self.controller.start_transmission(sat_filter, fc_sats, duration)
        
        # Initialize time tracking
        import time
        self.transmission_start_time = time.time()
        self.transmission_duration = duration
        
        self.start_btn.setEnabled(False)
        self.stop_btn.setEnabled(True)
        self.status_label.setText("Status: Transmitting")
        self.status_label.setStyleSheet("font-weight: bold; color: green;")
    
    def _stop_transmission(self):
        """Stop transmission"""
        self.controller.stop_transmission()
        
        # Reset time tracking
        self.transmission_start_time = None
        self.transmission_duration = 0
        self.time_elapsed_label.setText("Time Elapsed: --:--:--")
        self.time_remaining_label.setText("Time Remaining: --:--:--")
        
        self.start_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)
        self.status_label.setText("Status: Stopped")
        self.status_label.setStyleSheet("font-weight: bold; color: red;")
    
    def _load_message(self):
        """Load a message for editing"""
        sat_id = self.msg_sat_spin.value()
        msg_type_text = self.msg_type_combo.currentText()
        msg_type = int(msg_type_text.split()[0][2:])  # Extract number from "MTxx - ..."
        
        # Load message into field editor
        self.field_editor.load_message(sat_id, msg_type)
    
    def _send_message_once(self):
        """Send the current message once"""
        sat_id = self.msg_sat_spin.value()
        msg_type_text = self.msg_type_combo.currentText()
        msg_type = int(msg_type_text.split()[0][2:])
        
        self.controller.send_message_once(sat_id, msg_type)
    
    def _start_continuous_tx(self):
        """Start continuous transmission of current message"""
        sat_id = self.msg_sat_spin.value()
        msg_type_text = self.msg_type_combo.currentText()
        msg_type = int(msg_type_text.split()[0][2:])
        
        self.controller.start_continuous_tx(sat_id, msg_type)
        
        self.start_continuous_btn.setEnabled(False)
        self.stop_continuous_btn.setEnabled(True)
    
    def _stop_continuous_tx(self):
        """Stop continuous transmission"""
        self.controller.stop_continuous_tx()
        
        self.start_continuous_btn.setEnabled(True)
        self.stop_continuous_btn.setEnabled(False)
    
    def _orbit_model_changed(self, index):
        """Handle orbit model selection change"""
        self.tle_file_widget.setVisible(index == 1)  # Show TLE file selector when TLE selected
    
    def _browse_tle_file(self):
        """Browse for TLE file"""
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "Select TLE File",
            "",
            "TLE Files (*.txt *.tle);;All Files (*.*)"
        )
        if file_path:
            self.tle_path_edit.setText(file_path)
    
    def _apply_orbit_model(self):
        """Apply the selected orbit model"""
        model_index = self.orbit_model_combo.currentIndex()
        
        if model_index == 0:  # None
            self.controller.disable_orbit_model()
        elif model_index == 1:  # TLE
            tle_file = self.tle_path_edit.text()
            if not tle_file:
                QMessageBox.warning(self, "No TLE File", "Please select a TLE file.")
                return
            self.controller.set_orbit_model_tle(tle_file)
        elif model_index == 2:  # Statistical 128
            self.controller.set_orbit_model_statistical(128)
        elif model_index == 3:  # Statistical 143
            self.controller.set_orbit_model_statistical(143)
    
    def _browse_rinex_output(self):
        """Browse for RINEX output file"""
        file_path, _ = QFileDialog.getSaveFileName(
            self,
            "Select RINEX Output File",
            "",
            "RINEX Files (*.25B *.26B);;All Files (*.*)"
        )
        if file_path:
            self.rinex_path_edit.setText(file_path)
    
    def _enable_rinex_output(self):
        """Enable RINEX output"""
        output_path = self.rinex_path_edit.text()
        if not output_path:
            QMessageBox.warning(self, "No Output File", "Please specify an output file.")
            return
        
        template_path = self.file_path_edit.text()
        if not template_path:
            QMessageBox.warning(self, "No Template", "Please load an input file first (used as template).")
            return
        
        self.controller.configure_rinex_output(output_path, template_path)
    
    def _disable_rinex_output(self):
        """Disable RINEX output"""
        self.controller.configure_rinex_output(None, None)
    
    def _update_status(self):
        """Update status displays (called by timer)"""
        try:
            # Update client count
            client_count = self.controller.get_client_count()
            self.clients_label.setText(f"Connected Clients: {client_count}")
            
            # Update message count
            msg_count = self.controller.get_message_count()
            self.messages_label.setText(f"Loaded Messages: {msg_count}")
            
            # Update transmission time tracking
            if self.transmission_start_time is not None:
                import time
                elapsed = int(time.time() - self.transmission_start_time)
                
                # Format elapsed time as HH:MM:SS
                hours = elapsed // 3600
                minutes = (elapsed % 3600) // 60
                seconds = elapsed % 60
                elapsed_str = f"{hours:02d}:{minutes:02d}:{seconds:02d}"
                self.time_elapsed_label.setText(f"Time Elapsed: {elapsed_str}")
                
                # Calculate and display remaining time
                if self.transmission_duration > 0:  # Not continuous
                    remaining = self.transmission_duration - elapsed
                    if remaining < 0:
                        remaining = 0
                    
                    hours_r = remaining // 3600
                    minutes_r = (remaining % 3600) // 60
                    seconds_r = remaining % 60
                    remaining_str = f"{hours_r:02d}:{minutes_r:02d}:{seconds_r:02d}"
                    self.time_remaining_label.setText(f"Time Remaining: {remaining_str}")
                    
                    # Auto-stop when duration is reached
                    if remaining <= 0 and self.stop_btn.isEnabled():
                        self._stop_transmission()
                else:
                    # Continuous mode
                    self.time_remaining_label.setText("Time Remaining: Continuous")
            
        except Exception as e:
            # Silently ignore errors during periodic updates
            pass
    
    def append_log(self, message):
        """Append message to the log display"""
        self.log_display.append(message)
        # Auto-scroll to bottom
        scrollbar = self.log_display.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())
    
    def _show_about(self):
        """Show about dialog"""
        QMessageBox.about(
            self,
            "About Hercules Transmitter",
            "<h2>Hercules SBAS Transmitter System</h2>"
            "<p>Version 1.0.0</p>"
            "<p>A complete SBAS message processing and transmission system.</p>"
            "<p>© 2026</p>"
        )
    
    def closeEvent(self, event):
        """Handle window close"""
        reply = QMessageBox.question(
            self,
            "Confirm Exit",
            "Are you sure you want to exit?\nAny active transmission will be stopped.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            self.controller.cleanup()
            event.accept()
        else:
            event.ignore()
