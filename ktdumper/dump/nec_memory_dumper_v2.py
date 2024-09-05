from dump.nec_protocol_v2 import NecProtocol_v2
from dump.rw_access_v2 import RwAccess_v2
from dump.common_memory_dumper import CommonMemoryDumper


class NecMemoryDumper_v2(CommonMemoryDumper, RwAccess_v2, NecProtocol_v2):
    pass
