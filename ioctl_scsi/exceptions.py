
class SGSCSIException(Exception):
    """Base exception class for distinguishing our own exception classes."""
    pass

class BadCommandException(SGSCSIException):
    """Raised when a command is not fit for execution"""
    pass