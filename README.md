# ⚡ Monitor de Consumo Energético — UNIFEOB

> Sistema embarcado de medição de consumo de energia elétrica em tempo real, desenvolvido com **ESP32**, **ADS1115** e **transformadores de corrente (TCs)**. Os dados são publicados via MQTT em um broker público e exibidos localmente em um display LCD 16x2.

> Equipe:  
Alan da Silva Piana - RA: 26001149  
Johnny Phillip de Oliveira Tiengo - RA: 26000572  
Jonathan Wilian Rodrigues Silva - RA: 26000080  
Kauã da Silva Santos - RA: 26001488

---

## 📋 Índice

- [Visão Geral](#visão-geral)
- [Hardware](#hardware)
- [Esquema de Ligação](#esquema-de-ligação)
- [Dependências de Software](#dependências-de-software)
- [Configuração](#configuração)
- [Como Funciona](#como-funciona)
- [Tópicos MQTT](#tópicos-mqtt)
- [Calibração](#calibração)
- [Limitações Conhecidas](#limitações-conhecidas)
- [Melhorias Futuras](#melhorias-futuras)

---

## Visão Geral 🔍

O sistema monitora até **3 fases independentes** de corrente alternada (AC), calcula a potência ativa e acumula o consumo em kWh. A cada 5 segundos, os dados são publicados em um broker MQTT público (HiveMQ), possibilitando integração com dashboards como Node-RED, Home Assistant ou Grafana.

```
[TC 100A/50mA] ──┐
[TC 100A/50mA] ──┤── [ADS1115] ── I2C ── [ESP32] ── WiFi ── [MQTT]
[TC 100A/50mA] ──┘                            │
                                         [LCD 16x2 I2C]
```

---

## Hardware

| Componente | Especificação | Quantidade |
|---|---|:---:|
| Microcontrolador | ESP32 DevKit V1 | 1 |
| ADC externo | ADS1115 (16 bits, I2C) | 1 |
| Transformador de Corrente | TC 100A / 50mA | 3 |
| Resistor burden | 33 Ω / 0,5 W | 3 |
| Display | LCD 16x2 com módulo I2C (PCF8574) | 1 |

> **Por que o ADS1115?**
> O ADC interno do ESP32 tem linearidade ruim e não possui referência estável o suficiente para medições de corrente AC com boa precisão. O ADS1115 oferece 16 bits de resolução com ganho configurável, ideal para sinais de baixa amplitude como o gerado pelo TC sobre o resistor burden.

---

## Esquema de Ligação

### Barramento I2C (ESP32 → ADS1115 e LCD)

| ESP32 | ADS1115 | LCD I2C |
|---|---|---|
| GPIO 21 | SDA | SDA |
| GPIO 22 | SCL | SCL |
| 3.3V | VDD | VCC |
| GND | GND | GND |

### Entradas Analógicas (ADS1115)

| Canal ADS1115 | Fase | TC |
|---|---|---|
| A0 | Fase A | TC1 |
| A1 | Fase B | TC2 |
| A2 | Fase C | TC3 |

### Circuito do TC com Resistor Burden

Cada TC deve ter o resistor burden de **33 Ω** conectado em paralelo nos seus terminais de saída, antes de chegar ao ADS1115. O fio passa pelo buraco central do TC sem contato elétrico — é a medição por indução magnética.

```
        ┌─────────────────┐
 Fase ──┤  TC 100A/50mA   ├── S1 ──┬── A0 (ADS1115)
        └─────────────────┘        │
                                  [33Ω]
                                   │
                            S2 ── GND
```

> ⚠️ **ATENÇÃO:** Nunca deixe o TC com o secundário em aberto enquanto há corrente no primário. Isso gera tensões perigosas. O resistor burden deve estar **sempre** conectado.

---

## Dependências de Software

Instale as bibliotecas abaixo pela **Arduino IDE** (Gerenciador de Bibliotecas) ou **PlatformIO**:

| Biblioteca | Versão testada |
|---|---|
| Adafruit ADS1X15 | ≥ 2.4.0 |
| LiquidCrystal I2C | ≥ 1.1.2 |
| PubSubClient | ≥ 2.8 |
| WiFi | nativa ESP32 |
| Wire | nativa ESP32 |

---

## Configuração

Antes de compilar, edite as seguintes constantes no início do arquivo `.ino`:

```cpp
// Wi-Fi
const char* ssid     = "SEU_SSID";
const char* password = "SUA_SENHA";

// Parâmetros da instalação elétrica
const float TENSAO_NOMINAL = 127.0;   // ou 220.0 para rede 220V
const float FATOR_POTENCIA = 0.92;    // ajuste conforme a carga real
```

Se utilizar um broker MQTT privado, altere também:

```cpp
const char* mqttServer = "broker.hivemq.com";
const int   mqttPort   = 1883;
```

---

## Como Funciona

### 1. Leitura de Corrente RMS

O TC converte a corrente do cabo de força em uma corrente proporcional no secundário. Essa corrente gera uma tensão sobre o resistor burden de 33 Ω, que é lida pelo ADS1115.

O firmware coleta **300 amostras** por canal e aplica o cálculo de RMS:

$$V_{rms} = \sqrt{\frac{\sum V_i^2}{N}}$$

$$I_{rms} = \frac{V_{rms}}{R_{burden}} \times Ratio_{TC}$$

### 2. Cálculo de Potência

```
P (W) = Vnominal × Irms × FatorDePotência
```

A potência total é a soma das três fases.

### 3. Acumulação de Energia (kWh)

A cada ciclo do `loop()`, o código integra numericamente a potência ao longo do tempo:

```
ΔkWh = (Ptotal / 1000) × Δt(horas)
```

> ⚠️ O valor de kWh é armazenado apenas em **RAM**. Ele é zerado ao reiniciar o ESP32.

### 4. Display LCD

O display alterna automaticamente entre os 3 canais a cada **2 segundos**, exibindo:

- **Linha 1:** Canal, corrente (A) e tensão nominal
- **Linha 2:** Potência (W) e energia acumulada (kWh)

---

## Tópicos MQTT

Todos os tópicos são publicados no broker `broker.hivemq.com:1883` (sem autenticação).

| Tópico | Conteúdo | Exemplo |
|---|---|---|
| `unifeob/lab1/energia/faseA` | Corrente fase A (A) | `12.45` |
| `unifeob/lab1/energia/faseB` | Corrente fase B (A) | `08.30` |
| `unifeob/lab1/energia/faseC` | Corrente fase C (A) | `15.10` |
| `unifeob/lab1/energia/total` | Potência total (W) | `4198.50` |
| `unifeob/lab1/energia/kwh` | Energia acumulada (kWh) | `0.0583` |
| `unifeob/lab1/energia/status` | Status do dispositivo | `online` |

---

## Calibração

O ganho `GAIN_SIXTEEN` do ADS1115 foi escolhido para a faixa **±0,256 V**, adequada à tensão gerada pelo TC sobre o burden de 33 Ω com correntes de até ~15 A RMS no secundário (equivalente a 100 A no primário).

| Parâmetro | Valor | Origem |
|---|---|---|
| `ADS_SCALE` | 7,8 µV/bit | ±0,256V ÷ 2¹⁵ |
| `BURDEN_OHM` | 33 Ω | Resistor físico |
| `TC_RATIO` | 2000 | 100A ÷ 50mA |
| `NUM_SAMPL` | 300 | ~3 ciclos de 60Hz a ~100 leituras/ciclo |

> Para refinar a calibração, compare a leitura do sistema com um **multímetro true-RMS** e ajuste `TC_RATIO` proporcionalmente.

---

## Limitações Conhecidas

- **kWh volátil:** o acumulador é perdido a cada reinicialização (sem persistência em flash/EEPROM).
- **Fator de potência fixo:** o sistema usa um FP constante (0,92) em vez de medição real com defasagem tensão/corrente.
- **Broker público:** o HiveMQ público não garante disponibilidade nem privacidade dos dados.
- **Sem sincronismo de fase:** as leituras dos 3 canais são sequenciais, não simultâneas.
- **Sem NTP:** sem relógio sincronizado, não há timestamp nos dados publicados.

---

## Melhorias Futuras

- [ ] Persistir kWh acumulado na memória NVS/EEPROM do ESP32 ou armazenando em nuvem
- [ ] Adicionar timestamp via NTP nos payloads MQTT
- [ ] Migrar para broker privado com autenticação (MQTT over TLS)
- [ ] Calcular fator de potência real com sensor de tensão (ZMPT101B)
- [ ] Publicar payload em JSON em vez de valores isolados
- [ ] Leituras simultâneas das 3 fases (ADS1115 em modo diferencial ou segundo ADS)
- [ ] Dashboard Node-RED ou Grafana + InfluxDB para histórico

---

## Licença 📄

Projeto acadêmico desenvolvido para a unidade de estudo de **Internet das Coisas — UNIFEOB**.  
Uso livre para fins educacionais.
