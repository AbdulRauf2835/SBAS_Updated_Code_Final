"""
controller.py
-------------
Controller Layer: Coordinates between GUI (Presentation) and DLL API.
Handles threading, state management, and error handling.
"""

import threading
import time
from pathlib import Path
from api_wrapper import HerculesAPI, HerculesAPIError


class HerculesController:
    """
    Controller managing the connection between PyQt6 GUI and C DLL API.
    
    Responsibilities:
    - Initialize and manage HerculesAPI instance
    - Coordinate multi-threaded operations (transmission vs GUI updates)
    - Handle errors and status updates
    - Translate GUI actions into API calls
    - Maintain application state
    """
    
    def __init__(self, dll_path=None, tcp_port=23):
        """
        Initialize the controller
        
        Args:
            dll_path: Path to hercules_transmitter.dll (None = auto-detect)
            tcp_port: TCP port for transmission server (default 23)
        """
        self.api = HerculesAPI(dll_path)
        self.tcp_port = tcp_port
        self.main_window = None
        
        # State
        self.is_initialized = False
        self.is_transmitting = False
        self.is_continuous_tx = False
        self.transmission_thread = None
        
        # Setup status callback
        self.api.set_status_callback(self._status_callback)
    
    def set_main_window(self, window):
        """Set reference to main window for log updates"""
        self.main_window = window
    
    def initialize(self):
        """Initialize the transmitter system"""
        try:
            self.api.init(self.tcp_port)
            self.is_initialized = True
            self._log("System initialized on TCP port {}".format(self.tcp_port))
        except HerculesAPIError as e:
            self._log_error("Initialization failed: {}".format(str(e)))
            raise
    
    def bind_file(self, file_path):
        """
        Bind a RINEX .25B file
        
        Args:
            file_path: Path to RINEX file
        """
        try:
            self.api.bind_file(file_path)
            self._log("File loaded: {}".format(Path(file_path).name))
        except HerculesAPIError as e:
            self._log_error("Failed to load file: {}".format(str(e)))
            raise
    
    def start_transmission(self, sat_filter=0, fc_sats=0, duration=0):
        """
        Start message transmission
        
        Args:
            sat_filter: Satellite filter (0 = all)
            fc_sats: Number of fast correction satellites (0 = all MT2-5)
            duration: Duration in seconds (0 = continuous)
        """
        if self.is_transmitting:
            self._log_error("Transmission already active")
            return
        
        try:
            self.api.start_transmission(sat_filter, fc_sats, duration)
            self.is_transmitting = True
            
            # Start transmission thread
            self.transmission_thread = threading.Thread(
                target=self._transmission_loop,
                args=(duration,),
                daemon=True
            )
            self.transmission_thread.start()
            
            filter_msg = "all satellites" if sat_filter == 0 else "satellite {}".format(sat_filter)
            fc_msg = "all MT2-5" if fc_sats == 0 else "{} satellites".format(fc_sats)
            dur_msg = "continuous" if duration == 0 else "{} seconds".format(duration)
            
            self._log("Transmission started: {} | FC: {} | Duration: {}".format(
                filter_msg, fc_msg, dur_msg
            ))
            
        except HerculesAPIError as e:
            self._log_error("Failed to start transmission: {}".format(str(e)))
            raise
    
    def stop_transmission(self):
        """Stop message transmission"""
        if not self.is_transmitting:
            self._log_error("No active transmission to stop")
            return
        
        try:
            self.api.stop_transmission()
            self.is_transmitting = False
            
            # Wait for thread to finish
            if self.transmission_thread and self.transmission_thread.is_alive():
                self.transmission_thread.join(timeout=2.0)
            
            self._log("Transmission stopped")
            
        except HerculesAPIError as e:
            self._log_error("Failed to stop transmission: {}".format(str(e)))
            raise
    
    def _transmission_loop(self, duration):
        """
        Background transmission loop
        
        Args:
            duration: Duration in seconds (0 = continuous)
        """
        start_time = time.time()
        
        while self.is_transmitting:
            # Process transmission (accept clients, send messages)
            try:
                self.api.process_transmission()
            except Exception as e:
                self._log_error("Transmission error: {}".format(str(e)))
            
            # Check duration
            if duration > 0:
                elapsed = time.time() - start_time
                if elapsed >= duration:
                    self._log("Duration timer expired")
                    self.is_transmitting = False
                    break
            
            # Sleep 1 second between scheduler steps (matches console app)
            time.sleep(1.0)
    
    def get_client_count(self):
        """Get number of connected clients"""
        try:
            return self.api.get_client_count()
        except HerculesAPIError:
            return 0
    
    def get_message_count(self):
        """Get number of loaded messages"""
        try:
            return self.api.get_message_count()
        except HerculesAPIError:
            return 0
    
    def get_message(self, sat_id, msg_type):
        """
        Get message data for editing
        
        Args:
            sat_id: Satellite ID
            msg_type: Message type
            
        Returns:
            String representation of message data
        """
        try:
            # This is a placeholder - full implementation requires
            # message field extraction from DLL
            msg_info = self.api.get_message(sat_id, msg_type)
            
            # Format as editable text
            lines = []
            lines.append("Satellite: {}".format(sat_id))
            lines.append("Message Type: {}".format(msg_type))
            lines.append("")
            lines.append("Message Data:")
            lines.append(str(msg_info))
            
            return "\n".join(lines)
            
        except HerculesAPIError as e:
            self._log_error("Failed to load message: {}".format(str(e)))
            return "Error loading message"
    
    def get_message(self, sat_id, msg_type):
        """
        Get message data as JSON
        
        Args:
            sat_id: Satellite ID
            msg_type: Message type
            
        Returns:
            JSON string with message fields
        """
        try:
            return self.api.get_message(sat_id, msg_type)
        except HerculesAPIError as e:
            self._log_error("Failed to get message: {}".format(str(e)))
            raise
    
    def update_message_field(self, sat_id, msg_type, field_name, value):
        """
        Update a single message field
        
        Args:
            sat_id: Satellite ID
            msg_type: Message type
            field_name: Field name (e.g., "iodp", "xg_ecef")
            value: New value (will be converted to string)
        """
        try:
            self.api.update_message_field(sat_id, msg_type, field_name, value)
            self._log("Updated {}.{} = {}".format(msg_type, field_name, value))
        except HerculesAPIError as e:
            self._log_error("Failed to update field: {}".format(str(e)))
            raise
    
    def enable_field_increment(self, sat_id, msg_type):
        """
        Enable increment mode for continuous transmission
        
        After calling update_message_field(), call this to enable auto-increment.
        First transmission uses exact values, subsequent transmissions increment.
        
        Args:
            sat_id: Satellite ID
            msg_type: Message type
        """
        try:
            self.api.enable_field_increment(sat_id, msg_type)
            self._log("Increment mode enabled for SAT{} MT{}".format(sat_id, msg_type))
        except HerculesAPIError as e:
            self._log_error("Failed to enable increment: {}".format(str(e)))
            raise
    
    def send_message_once(self, sat_id, msg_type):
        """
        Send a single message once
        
        Args:
            sat_id: Satellite ID
            msg_type: Message type
        """
        try:
            # TODO: Implement single message send via API
            self._log("Sending message: SAT{} MT{}".format(sat_id, msg_type))
            
        except HerculesAPIError as e:
            self._log_error("Failed to send message: {}".format(str(e)))
            raise
    
    def start_continuous_tx(self, sat_id, msg_type):
        """
        Start continuous transmission of a single message
        
        Args:
            sat_id: Satellite ID
            msg_type: Message type
        """
        try:
            self.is_continuous_tx = True
            # TODO: Implement continuous single message transmission
            self._log("Continuous transmission started: SAT{} MT{}".format(sat_id, msg_type))
            
        except HerculesAPIError as e:
            self._log_error("Failed to start continuous TX: {}".format(str(e)))
            raise
    
    def stop_continuous_tx(self):
        """Stop continuous single message transmission"""
        try:
            self.is_continuous_tx = False
            # TODO: Implement stop continuous
            self._log("Continuous transmission stopped")
            
        except HerculesAPIError as e:
            self._log_error("Failed to stop continuous TX: {}".format(str(e)))
            raise
    
    def set_orbit_model_tle(self, tle_file_path):
        """
        Configure TLE orbit model
        
        Args:
            tle_file_path: Path to TLE file
        """
        try:
            self.api.set_orbit_model_tle(tle_file_path, 0, 0)  # Default mappings
            self._log("TLE orbit model enabled: {}".format(Path(tle_file_path).name))
            
        except HerculesAPIError as e:
            self._log_error("Failed to set TLE model: {}".format(str(e)))
            raise
    
    def set_orbit_model_statistical(self, sat_number):
        """
        Configure statistical orbit model
        
        Args:
            sat_number: Satellite number (128 or 143)
        """
        try:
            self.api.set_orbit_model_statistical(sat_number)
            self._log("Statistical orbit model enabled for satellite {}".format(sat_number))
            
        except HerculesAPIError as e:
            self._log_error("Failed to set statistical model: {}".format(str(e)))
            raise
    
    def disable_orbit_model(self):
        """Disable orbit model (use file data)"""
        try:
            # TODO: Add disable function to API
            self._log("Orbit model disabled (using file data)")
            
        except HerculesAPIError as e:
            self._log_error("Failed to disable orbit model: {}".format(str(e)))
            raise
    
    def configure_rinex_output(self, output_path, template_path):
        """
        Configure RINEX output file
        
        Args:
            output_path: Output file path (None to disable)
            template_path: Template file path (for header copy)
        """
        try:
            if output_path is None:
                self.api.configure_rinex_output(None, None)
                self._log("RINEX output disabled")
            else:
                self.api.configure_rinex_output(output_path, template_path)
                self._log("RINEX output enabled: {}".format(Path(output_path).name))
                
        except HerculesAPIError as e:
            self._log_error("Failed to configure RINEX output: {}".format(str(e)))
            raise
    
    def _status_callback(self, status_msg):
        """
        Callback for status messages from DLL
        
        Args:
            status_msg: Status message string from C API
        """
        self._log("[DLL] {}".format(status_msg))
    
    def _log(self, message):
        """
        Log a message to the GUI
        
        Args:
            message: Message to log
        """
        if self.main_window:
            self.main_window.append_log(message)
        else:
            print(message)
    
    def _log_error(self, message):
        """
        Log an error message
        
        Args:
            message: Error message
        """
        error_msg = "ERROR: {}".format(message)
        self._log(error_msg)
    
    def cleanup(self):
        """Cleanup resources before exit"""
        try:
            if self.is_transmitting:
                self.stop_transmission()
            
            if self.is_continuous_tx:
                self.stop_continuous_tx()
            
            self.api.cleanup()
            self._log("System shutdown complete")
            
        except Exception as e:
            self._log_error("Cleanup error: {}".format(str(e)))
