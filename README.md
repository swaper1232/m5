# M5 BLE Keyboard Project

## Слоты (версии прошивки)

### Slot 1
- Полная инициализация BLE стека при переключении режимов
- Корректная работа HID и сканера
- Стабильное переподключение
- Правильная обработка безопасности
- Много избыточных шагов при переключении режимов

### Slot 2
- Оптимизированное переключение режимов согласно документации NimBLE
- Минимально необходимые шаги для реконнекта:
  1. Остановка сканирования и очистка результатов
  2. Остановка рекламы
  3. Перезапуск HID сервиса
  4. Запуск рекламы
- Не требуется:
  - Deinit/init стека
  - Пересоздание сервера
  - Пересоздание HID устройства
  - Переустановка настроек безопасности
- Более быстрое переключение режимов
- Стабильная работа

## Особенности работы
- Кнопка A: отправка тестового символа 'a'
- Кнопка B: переключение между режимами HID и сканирования
- Отображение статуса подключения и RSSI на экране 