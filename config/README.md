# config

运行时配置目录。正式实现由 `JsonConfigStore` 管理；不得由 UI 直接读写 JSON。

需要保留旧版业务语义：配置键至少包含 `workbook identity + sheet + header row`，列映射包含 selected/role/language/tts_engine。
