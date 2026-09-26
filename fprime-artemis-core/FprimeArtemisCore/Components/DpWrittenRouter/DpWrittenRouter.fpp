module Components {
    @ Fans out data-product write notifications to the catalog and mission payload driver.
    passive component DpWrittenRouter {

        @ Data product write notification from DpWriter
        sync input port dpWrittenIn: Svc.DpWritten

        @ Catalog notification output
        output port catalogOut: Svc.DpWritten

        @ Lepton payload-driver notification output
        output port leptonNotifyOut: Svc.DpWritten

        @ Boson payload-driver notification output
        output port bosonNotifyOut: Svc.DpWritten
    }
}
