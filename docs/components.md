# Component Diagram

```mermaid
graph TB
    subgraph Core Domain
        VS[VehicleSignal]
        VC[VehicleConfig]
        VCR[VehicleConfigRegistry]
        G[Gear.h]
        ISS[ISignalSource]
        DSS[DemoSignalSource]
        BSS[BLESignalSource]
    end

    subgraph DBC Layer
        DP[DBCParser]
        DSD[DBCSignalDefinition]
        DSM[DBCSignalMapper]
        DST[DBCSignalTranslator]
        DTS[DBCTranslationService]
    end

    subgraph Boundary Layer
        BM[BLEManager]
        ELM[ELM327Transport]
        OST[OBD2SignalTranslator]
        OSTB[OBD2SignalTranslatorBase]
    end

    subgraph Signal Sources
        DPV[DemoSignalProvider]
    end

    subgraph Presentation
        VSF[VehicleSignalFormatter]
        TL[TraceLogger]
        RTL[RawTraceLogger]
    end

    subgraph iOS Bridge
        VSW[VehicleSimWrapper]
        ICP[IOSDBCContentProvider]
        VVM[VehicleViewModel]
        CV[ContentView]
    end

    subgraph CLI
        CLI[CliOptions]
    end

    VCR -->|registers| VC
    VC -->|contains| signalMappings[signalMappings]
    VC -->|contains| dbcFilePath
    VC -->|contains| dbcBundleFileName

    ISS -.->|implements| DSS
    ISS -.->|implements| BSS

    DTS -->|creates| DST
    DTS -->|uses| VCR
    DST -->|uses| DSM
    DST -->|uses| DP
    DST -->|produces| VS
    DSM -->|uses| G

    DSS -->|uses| DPV
    BSS -->|uses| BM
    BM -->|uses| DTS

    VS -->|consumed by| VSF
    VS -->|consumed by| TL
    VS -->|consumed by| RTL

    VSW -->|owns| BSS
    VSW -->|owns| DSS
    VSW -->|owns| BM
    VSW -->|owns| DTS
    VSW -->|uses| ICP

    VSW -->|exposes| VVM
    VVM -->|binds to| CV

    CLI -->|uses| DTS
```

## Component Responsibilities

### Core Domain
| Component | Responsibility |
|-----------|---------------|
| VehicleSignal | Immutable signal container |
| VehicleConfig | Vehicle configuration with signal mappings |
| VehicleConfigRegistry | Registry of known vehicle configs |
| Gear.h | Canonical gear constants |
| ISignalSource | Abstract signal source interface |
| DemoSignalSource | Synthetic signal generation |
| BLESignalSource | Live BLE data with DBC translation |

### DBC Layer
| Component | Responsibility |
|-----------|---------------|
| DBCParser | Parses DBC files into structured data |
| DBCSignalDefinition | DBC signal/field definitions |
| DBCSignalMapper | Translates CAN values to domain types (gear translation) |
| DBCSignalTranslator | Translates CAN frames to VehicleSignal |
| DBCTranslationService | Orchestrates DBC loading and translation |

### Boundary Layer
| Component | Responsibility |
|-----------|---------------|
| BLEManager | BLE device management and data handling |
| ELM327Transport | ELM327 protocol handling |
| OBD2SignalTranslator | OBD2 PID translation |

### Presentation
| Component | Responsibility |
|-----------|---------------|
| VehicleSignalFormatter | Formats signals for display |
| TraceLogger | Event logging |
| RawTraceLogger | Raw CAN frame logging |

### iOS Bridge
| Component | Responsibility |
|-----------|---------------|
| VehicleSimWrapper | Thin veneer bridging C++ to Swift |
| IOSDBCContentProvider | iOS bundle DBC loading |
| VehicleViewModel | SwiftUI view model |
| ContentView | Main SwiftUI view |

## Removed Components (dead code)
The following components were removed as part of the DBC architecture cleanup:

- `CANTranslatorBase`/`.h` — Base class no longer needed
- `CANSignalDecoderBase`/`.h` — Hand-crafted decoding replaced by DBC parser
- `AudiMLBTranslator`/`.h` — Vehicle-specific code replaced by DBC
- `TeslaCANTranslator`/`.h` — Vehicle-specific code replaced by DBC
- `AudiSignalTranslator`/`.h` — Vehicle-specific code replaced by DBC
- `TeslaSignalTranslator`/`.h` — Vehicle-specific code replaced by DBC
- `TeslaSignalParser`/`.h` — Vehicle-specific code replaced by DBC
- `SignalTranslatorFactory`/`.h` — Vehicle-specific factory replaced by generic DBCSignalTranslator