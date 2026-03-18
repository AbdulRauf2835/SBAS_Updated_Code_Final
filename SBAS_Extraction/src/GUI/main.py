"""
main.py
-------
Entry point for Hercules Transmitter GUI application.
Initializes the system, creates the Qt application, and launches the main window.
"""

import sys
from pathlib import Path
from PyQt6.QtWidgets import QApplication, QMessageBox
from PyQt6.QtCore import Qt

# Add parent directory to path for imports
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from controller import HerculesController
from main_window import MainWindow


def main():
    """Main entry point for the GUI application"""
    
    # Create Qt Application
    app = QApplication(sys.argv)
    app.setApplicationName("Hercules SBAS Transmitter")
    app.setOrganizationName("SBAS Research")
    
    # Set application style
    app.setStyle("Fusion")
    
    try:
        # Initialize controller
        # DLL should be in project root (hercules_transmitter.dll)
        dll_path = Path(__file__).parent.parent.parent / "hercules_transmitter.dll"
        
        if not dll_path.exists():
            QMessageBox.critical(
                None,
                "DLL Not Found",
                f"Could not find hercules_transmitter.dll at:\n{dll_path}\n\n"
                "Please build the DLL first:\n"
                "  make hercules_transmitter.dll"
            )
            return 1
        
        controller = HerculesController(dll_path=str(dll_path), tcp_port=23)
        
        # Create main window
        window = MainWindow(controller)
        controller.set_main_window(window)
        
        # Initialize the transmitter system
        try:
            controller.initialize()
        except Exception as e:
            QMessageBox.critical(
                None,
                "Initialization Failed",
                f"Failed to initialize transmitter system:\n{str(e)}"
            )
            return 1
        
        # Show window
        window.show()
        
        # Run application
        return app.exec()
        
    except Exception as e:
        QMessageBox.critical(
            None,
            "Fatal Error",
            f"An unexpected error occurred:\n{str(e)}\n\n"
            "The application will now exit."
        )
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
