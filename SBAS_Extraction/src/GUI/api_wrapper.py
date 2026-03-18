"""
api_wrapper.py
--------------
API Layer: Python wrapper for hercules_transmitter.dll using ctypes.
This module provides a Pythonic interface to the C DLL functions.
"""

import ctypes
from ctypes import c_int, c_char_p, c_size_t, c_void_p, POINTER, CFUNCTYPE
from pathlib import Path
import os

# Callback type for status updates
StatusCallbackType = CFUNCTYPE(None, c_char_p)


class HerculesAPIError(Exception):
    """Exception raised when Hercules API calls fail"""
    pass


class HerculesAPI:
    """
    Low-level wrapper for hercules_transmitter.dll
    
    This class loads the DLL and wraps all exported functions with proper
    type signatures for safe calling from Python.
    """
    
    def __init__(self, dll_path=None):
        """
        Initialize the API wrapper
        
        Args:
            dll_path: Path to hercules_transmitter.dll. If None, searches in standard locations.
        """
        if dll_path is None:
            # Try to find DLL in project root or current directory
            project_root = Path(__file__).parent.parent.parent
            dll_path = project_root / "hercules_transmitter.dll"
            
            if not dll_path.exists():
                dll_path = Path("hercules_transmitter.dll")
            
            if not dll_path.exists():
                raise FileNotFoundError(
                    "hercules_transmitter.dll not found. "
                    "Build it with: make hercules_transmitter.dll"
                )
        
        self._dll = ctypes.CDLL(str(dll_path))
        self._setup_function_signatures()
        self._status_callback = None
        self._c_callback = None
    
    def _setup_function_signatures(self):
        """Define argument and return types for all DLL functions"""
        
        # Initialization & Cleanup
        self._dll.hercules_init.argtypes = [c_int]
        self._dll.hercules_init.restype = c_int
        
        self._dll.hercules_cleanup.argtypes = []
        self._dll.hercules_cleanup.restype = None
        
        # File Binding
        self._dll.hercules_bind_file.argtypes = [c_char_p]
        self._dll.hercules_bind_file.restype = c_int
        
        self._dll.hercules_get_message_count.argtypes = []
        self._dll.hercules_get_message_count.restype = c_size_t
        
        self._dll.hercules_get_bound_file.argtypes = []
        self._dll.hercules_get_bound_file.restype = c_char_p
        
        # Transmission Control
        self._dll.hercules_start_transmission.argtypes = [c_int, c_int, c_int]
        self._dll.hercules_start_transmission.restype = c_int
        
        self._dll.hercules_stop_transmission.argtypes = []
        self._dll.hercules_stop_transmission.restype = None
        
        self._dll.hercules_process_transmission.argtypes = []
        self._dll.hercules_process_transmission.restype = c_int
        
        self._dll.hercules_is_transmitting.argtypes = []
        self._dll.hercules_is_transmitting.restype = c_int
        
        # Client Status
        self._dll.hercules_get_client_count.argtypes = []
        self._dll.hercules_get_client_count.restype = c_int
        
        # Message Editing
        self._dll.hercules_get_message.argtypes = [c_int, c_int, c_char_p, c_size_t]
        self._dll.hercules_get_message.restype = c_int
        
        self._dll.hercules_update_message_field.argtypes = [c_int, c_int, c_char_p, c_char_p]
        self._dll.hercules_update_message_field.restype = c_int
        
        self._dll.hercules_enable_field_increment.argtypes = [c_int, c_int]
        self._dll.hercules_enable_field_increment.restype = c_int
        
        self._dll.hercules_send_message_once.argtypes = [c_int, c_int]
        self._dll.hercules_send_message_once.restype = c_int
        
        self._dll.hercules_start_single_message_tx.argtypes = [c_int, c_int, c_int]
        self._dll.hercules_start_single_message_tx.restype = c_int
        
        self._dll.hercules_stop_single_message_tx.argtypes = []
        self._dll.hercules_stop_single_message_tx.restype = None
        
        # Orbit Model
        self._dll.hercules_set_orbit_model_tle.argtypes = [c_char_p, c_char_p]
        self._dll.hercules_set_orbit_model_tle.restype = c_int
        
        self._dll.hercules_set_orbit_model_statistical.argtypes = [c_int]
        self._dll.hercules_set_orbit_model_statistical.restype = c_int
        
        self._dll.hercules_disable_orbit_model.argtypes = []
        self._dll.hercules_disable_orbit_model.restype = None
        
        self._dll.hercules_set_tle_time_mode.argtypes = [c_int, c_char_p]
        self._dll.hercules_set_tle_time_mode.restype = c_int
        
        # RINEX Output
        self._dll.hercules_configure_rinex_output.argtypes = [c_char_p, c_char_p]
        self._dll.hercules_configure_rinex_output.restype = c_int
        
        self._dll.hercules_is_rinex_output_active.argtypes = []
        self._dll.hercules_is_rinex_output_active.restype = c_int
        
        # Status & Logging
        self._dll.hercules_get_last_error.argtypes = []
        self._dll.hercules_get_last_error.restype = c_char_p
        
        self._dll.hercules_get_transmission_stats.argtypes = [c_char_p, c_size_t]
        self._dll.hercules_get_transmission_stats.restype = c_int
        
        self._dll.hercules_set_status_callback.argtypes = [StatusCallbackType]
        self._dll.hercules_set_status_callback.restype = None
    
    # High-level wrapper methods
    
    def init(self, tcp_port=5000):
        """Initialize the transmitter system"""
        result = self._dll.hercules_init(tcp_port)
        if result != 0:
            raise HerculesAPIError(self.get_last_error())
        return result
    
    def cleanup(self):
        """Cleanup and shutdown"""
        self._dll.hercules_cleanup()
    
    def bind_file(self, file_path):
        """Bind a RINEX .25B file"""
        if isinstance(file_path, Path):
            file_path = str(file_path)
        
        result = self._dll.hercules_bind_file(file_path.encode('utf-8'))
        if result != 0:
            raise HerculesAPIError(self.get_last_error())
        return result
    
    def get_message_count(self):
        """Get number of loaded messages"""
        return self._dll.hercules_get_message_count()
    
    def get_bound_file(self):
        """Get path of currently bound file"""
        result = self._dll.hercules_get_bound_file()
        return result.decode('utf-8') if result else None
    
    def start_transmission(self, satellite_filter=0, fast_corr_sats=0, duration=0):
        """Start transmission"""
        result = self._dll.hercules_start_transmission(
            satellite_filter, fast_corr_sats, duration
        )
        if result != 0:
            raise HerculesAPIError(self.get_last_error())
        return result
    
    def stop_transmission(self):
        """Stop transmission"""
        self._dll.hercules_stop_transmission()
    
    def process_transmission(self):
        """Process one iteration of transmission (accept clients, send messages)
        Should be called periodically when transmission is active"""
        return self._dll.hercules_process_transmission()
    
    def is_transmitting(self):
        """Check if currently transmitting"""
        return bool(self._dll.hercules_is_transmitting())
    
    def get_client_count(self):
        """Get number of connected clients"""
        return self._dll.hercules_get_client_count()
    
    def get_message(self, satellite_id, message_type):
        """Get message data as JSON string"""
        buffer = ctypes.create_string_buffer(4096)
        result = self._dll.hercules_get_message(
            satellite_id, message_type, buffer, len(buffer)
        )
        if result != 0:
            raise HerculesAPIError(self.get_last_error())
        return buffer.value.decode('utf-8')
    
    def update_message_field(self, satellite_id, message_type, field_name, value):
        """Update a message field"""
        result = self._dll.hercules_update_message_field(
            satellite_id, message_type,
            field_name.encode('utf-8'),
            str(value).encode('utf-8')
        )
        if result != 0:
            raise HerculesAPIError(self.get_last_error())
        return result
    
    def enable_field_increment(self, satellite_id, message_type):
        """
        Enable field increment mode for continuous transmission.
        
        After updating fields, call this to enable auto-increment.
        First transmission uses exact values, subsequent ones increment.
        """
        result = self._dll.hercules_enable_field_increment(satellite_id, message_type)
        if result != 0:
            raise HerculesAPIError(self.get_last_error())
        return result
    
    def send_message_once(self, satellite_id, message_type):
        """Send a message once"""
        result = self._dll.hercules_send_message_once(satellite_id, message_type)
        if result != 0:
            raise HerculesAPIError(self.get_last_error())
        return result
    
    def start_single_message_tx(self, satellite_id, message_type, interval_ms=1000):
        """Start continuous transmission of a single message"""
        result = self._dll.hercules_start_single_message_tx(
            satellite_id, message_type, interval_ms
        )
        if result != 0:
            raise HerculesAPIError(self.get_last_error())
        return result
    
    def stop_single_message_tx(self):
        """Stop continuous single message transmission"""
        self._dll.hercules_stop_single_message_tx()
    
    def set_orbit_model_tle(self, tle_file_path, satellite_mappings="[]"):
        """Set TLE orbit model"""
        if isinstance(tle_file_path, Path):
            tle_file_path = str(tle_file_path)
        
        result = self._dll.hercules_set_orbit_model_tle(
            tle_file_path.encode('utf-8'),
            satellite_mappings.encode('utf-8')
        )
        if result != 0:
            raise HerculesAPIError(self.get_last_error())
        return result
    
    def set_orbit_model_statistical(self, satellite_id):
        """Set statistical orbit model (128 or 143)"""
        result = self._dll.hercules_set_orbit_model_statistical(satellite_id)
        if result != 0:
            raise HerculesAPIError(self.get_last_error())
        return result
    
    def disable_orbit_model(self):
        """Disable orbit model"""
        self._dll.hercules_disable_orbit_model()
    
    def set_tle_time_mode(self, use_simulation, base_time_utc=None):
        """Set TLE time mode"""
        time_str = base_time_utc.encode('utf-8') if base_time_utc else None
        result = self._dll.hercules_set_tle_time_mode(
            1 if use_simulation else 0,
            time_str
        )
        if result != 0:
            raise HerculesAPIError(self.get_last_error())
        return result
    
    def configure_rinex_output(self, output_path, template_path):
        """Configure RINEX output"""
        if isinstance(output_path, Path):
            output_path = str(output_path)
        if isinstance(template_path, Path):
            template_path = str(template_path)
        
        out_bytes = output_path.encode('utf-8') if output_path else None
        tmpl_bytes = template_path.encode('utf-8') if template_path else None
        
        result = self._dll.hercules_configure_rinex_output(out_bytes, tmpl_bytes)
        if result != 0:
            raise HerculesAPIError(self.get_last_error())
        return result
    
    def is_rinex_output_active(self):
        """Check if RINEX output is active"""
        return bool(self._dll.hercules_is_rinex_output_active())
    
    def get_last_error(self):
        """Get last error message"""
        result = self._dll.hercules_get_last_error()
        return result.decode('utf-8') if result else "Unknown error"
    
    def get_transmission_stats(self):
        """Get transmission statistics as JSON string"""
        buffer = ctypes.create_string_buffer(1024)
        result = self._dll.hercules_get_transmission_stats(buffer, len(buffer))
        if result != 0:
            return "{}"
        return buffer.value.decode('utf-8')
    
    def set_status_callback(self, callback):
        """
        Set callback for status updates
        
        Args:
            callback: Python function that takes a string argument
        """
        if callback is None:
            self._c_callback = None
            self._status_callback = None
            self._dll.hercules_set_status_callback(None)
        else:
            # Store both Python callback and C wrapper to prevent garbage collection
            self._status_callback = callback
            self._c_callback = StatusCallbackType(
                lambda msg: callback(msg.decode('utf-8') if msg else "")
            )
            self._dll.hercules_set_status_callback(self._c_callback)
