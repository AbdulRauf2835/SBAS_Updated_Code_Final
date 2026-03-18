"""
field_editor.py
---------------
Field Editor Widget: Structured editor for SBAS message fields.
Displays fields in an editable grid with Update button.
"""

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QLabel,
    QLineEdit, QPushButton, QScrollArea, QGroupBox, QMessageBox
)
from PyQt6.QtCore import Qt
import json
import re


def natural_sort_key(s):
    """
    Natural sort key for sorting strings with numbers.
    Makes "field[0]", "field[1]", "field[10]" sort correctly.
    """
    return [int(text) if text.isdigit() else text.lower()
            for text in re.split(r'(\d+)', s)]


class FieldEditorWidget(QWidget):
    """
    Widget for editing SBAS message fields in a structured grid.
    
    Displays field names and current values with editable inputs.
    Tracks which fields have been modified.
    Provides Update button to apply changes via API.
    """
    
    def __init__(self, controller):
        super().__init__()
        self.controller = controller
        self.satellite_id = None
        self.message_type = None
        self.original_data = {}
        self.field_inputs = {}  # Maps field_name -> QLineEdit widget
        
        self._setup_ui()
    
    def _setup_ui(self):
        """Setup the UI layout"""
        layout = QVBoxLayout(self)
        
        # Header
        header_layout = QHBoxLayout()
        self.msg_info_label = QLabel("No message loaded")
        self.msg_info_label.setStyleSheet("font-weight: bold; font-size: 12pt;")
        header_layout.addWidget(self.msg_info_label)
        header_layout.addStretch()
        layout.addLayout(header_layout)
        
        # Scrollable field grid
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        
        self.fields_widget = QWidget()
        self.fields_layout = QGridLayout(self.fields_widget)
        self.fields_layout.setColumnStretch(0, 1)  # Field name
        self.fields_layout.setColumnStretch(1, 2)  # Value input
        
        scroll.setWidget(self.fields_widget)
        layout.addWidget(scroll)
        
        # Action buttons
        button_layout = QHBoxLayout()
        button_layout.addStretch()
        
        self.update_btn = QPushButton("Update Fields")
        self.update_btn.setEnabled(False)
        self.update_btn.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold; padding: 8px;")
        self.update_btn.clicked.connect(self._update_fields)
        button_layout.addWidget(self.update_btn)
        
        self.reset_btn = QPushButton("Reset to Original")
        self.reset_btn.setEnabled(False)
        self.reset_btn.clicked.connect(self._reset_fields)
        button_layout.addWidget(self.reset_btn)
        
        layout.addLayout(button_layout)
    
    def load_message(self, satellite_id, message_type):
        """
        Load message data and populate field editor
        
        Args:
            satellite_id: Satellite ID
            message_type: Message type (1-28)
        """
        try:
            # Get message data as JSON
            json_str = self.controller.get_message(satellite_id, message_type)
            data = json.loads(json_str)
            
            # Extract only the fields object (not metadata like satellite_id, message_type)
            if "fields" in data:
                fields_data = data["fields"]
            else:
                fields_data = data
            
            # Store metadata
            self.satellite_id = satellite_id
            self.message_type = message_type
            self.original_data = fields_data.copy()
            
            # Update header
            self.msg_info_label.setText(f"Editing MT{message_type} - Satellite {satellite_id}")
            
            # Clear existing fields
            for i in reversed(range(self.fields_layout.count())):
                self.fields_layout.itemAt(i).widget().setParent(None)
            self.field_inputs.clear()
            
            # Flatten nested structures and add field rows
            row = 0
            flattened = self._flatten_dict(fields_data)
            
            for field_name, field_value in sorted(flattened.items()):
                # Field name label
                label = QLabel(field_name + ":")
                label.setStyleSheet("font-weight: bold;")
                self.fields_layout.addWidget(label, row, 0, Qt.AlignmentFlag.AlignRight)
                
                # Field value input
                input_widget = QLineEdit()
                input_widget.setText(str(field_value))
                input_widget.setPlaceholderText(f"Original: {field_value}")
                input_widget.textChanged.connect(self._on_field_changed)
                self.fields_layout.addWidget(input_widget, row, 1)
                
                # Store reference
                self.field_inputs[field_name] = input_widget
                
                row += 1
            
            # Enable buttons
            self.update_btn.setEnabled(True)
            self.reset_btn.setEnabled(True)
            
        except Exception as e:
            QMessageBox.critical(self, "Load Error", f"Failed to load message: {str(e)}")
    
    def _flatten_dict(self, d, parent_key='', sep='.'):
        """
        Flatten nested dict/list structures into dotted notation
        
        Examples:
            {"a": 1, "b": {"c": 2}} -> {"a": 1, "b.c": 2}
            {"arr": [{"x": 1}, {"x": 2}]} -> {"arr[0].x": 1, "arr[1].x": 2}
        """
        items = []
        
        if isinstance(d, dict):
            for k, v in d.items():
                new_key = f"{parent_key}{sep}{k}" if parent_key else k
                
                if isinstance(v, dict):
                    items.extend(self._flatten_dict(v, new_key, sep=sep).items())
                elif isinstance(v, list):
                    for i, item in enumerate(v):
                        if isinstance(item, dict):
                            items.extend(self._flatten_dict(item, f"{new_key}[{i}]", sep=sep).items())
                        else:
                            items.append((f"{new_key}[{i}]", item))
                else:
                    items.append((new_key, v))
        elif isinstance(d, list):
            for i, item in enumerate(d):
                new_key = f"{parent_key}[{i}]"
                if isinstance(item, dict):
                    items.extend(self._flatten_dict(item, new_key, sep=sep).items())
                else:
                    items.append((new_key, item))
        else:
            items.append((parent_key, d))
        
        return dict(items)
    
    def _on_field_changed(self):
        """Called when any field value changes"""
        # Highlight Update button when changes detected
        has_changes = self._has_changes()
        if has_changes:
            self.update_btn.setStyleSheet("background-color: #FF9800; color: white; font-weight: bold; padding: 8px;")
        else:
            self.update_btn.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold; padding: 8px;")
    
    def _has_changes(self):
        """Check if any fields have been modified"""
        for field_name, input_widget in self.field_inputs.items():
            current_value = input_widget.text()
            original_value = str(self.original_data.get(field_name, ""))
            if current_value != original_value:
                return True
        return False
    
    def _update_fields(self):
        """Apply field updates via API"""
        if not self.satellite_id or not self.message_type:
            return
        
        try:
            updated_count = 0
            
            # Update each modified field
            for field_name, input_widget in self.field_inputs.items():
                current_value = input_widget.text()
                original_value = str(self.original_data.get(field_name, ""))
                
                if current_value != original_value:
                    # Call API to update field
                    self.controller.update_message_field(
                        self.satellite_id,
                        self.message_type,
                        field_name,
                        current_value
                    )
                    updated_count += 1
            
            if updated_count > 0:
                # Enable increment mode for continuous transmission
                self.controller.enable_field_increment(self.satellite_id, self.message_type)
                
                # Update original data to match current
                for field_name, input_widget in self.field_inputs.items():
                    self.original_data[field_name] = input_widget.text()
                
                # Reset button color
                self.update_btn.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold; padding: 8px;")
                
                QMessageBox.information(
                    self,
                    "Update Complete",
                    f"Updated {updated_count} field(s).\n\n"
                    f"Increment mode ENABLED:\n"
                    f"- First transmission will use your exact values\n"
                    f"- Subsequent transmissions will auto-increment changed fields"
                )
            else:
                QMessageBox.information(self, "No Changes", "No fields were modified.")
                
        except Exception as e:
            QMessageBox.critical(self, "Update Error", f"Failed to update fields: {str(e)}")
    
    def _reset_fields(self):
        """Reset all fields to original values"""
        for field_name, input_widget in self.field_inputs.items():
            original_value = str(self.original_data.get(field_name, ""))
            input_widget.setText(original_value)
        
        # Reset button color
        self.update_btn.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold; padding: 8px;")
