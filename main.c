#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <stdio.h>

#include "Include/UART/UART.h"

/* UART0-modtagelse sker i interruptet; resten behandles i main-loekken. */
#define UART0_QUEUE_SIZE 32U
#define UART0_QUEUE_MASK (UART0_QUEUE_SIZE - 1U)

typedef enum {
  Modtag_Adresse,
  Modtag_Bit_Position_I_Adresse,
  Modtag_Bit_Positon_Vaerdi
} Tilstand;

static volatile char uart0_queue[UART0_QUEUE_SIZE];
static volatile uint8_t uart0_read;
static volatile uint8_t uart0_write;

static Tilstand tilstand;
static char adresse_tekst[7]; /* Maks. "0xFFFF" plus nul-tegn. */
static uint8_t adresse_laengde;
static volatile uint8_t *valgt_adresse;
static uint16_t modtaget_adresse;
static uint8_t bit_position;

static void UART0_Init(void) {
  UBRR0 = MyUBRR;
  UCSR0A = 0;
  UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);
  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

static void UART0_SendChar(char character) {
  while ((UCSR0A & (1 << UDRE0)) == 0) {
  }
  UDR0 = character;
}

static void UART0_SendTekst(const char *tekst) {
  while (*tekst != '\0') {
    UART0_SendChar(*tekst++);
  }
}

static uint8_t HexVaerdi(char character, uint8_t *vaerdi) {
  if (character >= '0' && character <= '9') {
    *vaerdi = (uint8_t)(character - '0');
    return 1;
  }
  if (character >= 'A' && character <= 'F') {
    *vaerdi = (uint8_t)(character - 'A' + 10);
    return 1;
  }
  if (character >= 'a' && character <= 'f') {
    *vaerdi = (uint8_t)(character - 'a' + 10);
    return 1;
  }
  return 0;
}

static uint8_t KonverterAdresse(uint16_t *adresse) {
  uint8_t indeks = 0;
  uint8_t ciffer;
  uint16_t resultat = 0;

  if (adresse_laengde >= 2 && adresse_tekst[0] == '0' &&
      (adresse_tekst[1] == 'x' || adresse_tekst[1] == 'X')) {
    indeks = 2;
  }
  if (indeks == adresse_laengde) {
    return 0;
  }

  for (; indeks < adresse_laengde; indeks++) {
    if (!HexVaerdi(adresse_tekst[indeks], &ciffer)) {
      return 0;
    }
    resultat = (uint16_t)((resultat << 4) | ciffer);
  }
  *adresse = resultat;
  return 1;
}

static void RydAdresse(void) {
  adresse_laengde = 0;
  adresse_tekst[0] = '\0';
}

static void SkiftTilstand(Tilstand ny_tilstand) {
  tilstand = ny_tilstand;

  switch (tilstand) {
  case Modtag_Adresse:
    UART0_SendTekst("\r\nTilstand: Modtag_Adresse\r\n"
                    "Indtast hexadecimal adresse og afslut med ':'.\r\n");
    break;
  case Modtag_Bit_Position_I_Adresse:
    UART0_SendTekst("Tilstand: Modtag_Bit_Position_I_Adresse\r\n"
                    "Indtast bit-position 0-7.\r\n");
    break;
  case Modtag_Bit_Positon_Vaerdi:
    UART0_SendTekst("Tilstand: Modtag_Bit_Positon_Vaerdi\r\n"
                    "Indtast bit-vaerdi 0 eller 1.\r\n");
    break;
  }
}

static void UdskrivResultat(uint8_t bit_vaerdi) {
  char linje[24];
  uint8_t port_vaerdi = *valgt_adresse;

  snprintf(linje, sizeof(linje), "Adresse: 0x%04X\r\n", modtaget_adresse);
  UART0_SendTekst(linje);
  snprintf(linje, sizeof(linje), "Bit position: %u\r\n", bit_position);
  UART0_SendTekst(linje);
  snprintf(linje, sizeof(linje), "Bit vaerdi: %u\r\n", bit_vaerdi);
  UART0_SendTekst(linje);
  UART0_SendTekst("Bitmoenster: ");

  for (int8_t i = 7; i >= 0; i--) {
    UART0_SendChar((port_vaerdi & (1U << i)) ? '1' : '0');
    if (i == 4) {
      UART0_SendChar(' ');
    }
  }
  UART0_SendTekst("\r\n");
}

static void BehandlTegn(char character) {
  uint8_t hex_vaerdi;

  /* Terminalprogrammer tilfoejer ofte CR/LF; det er ikke en del af inputtet. */
  if (character == '\r' || character == '\n') {
    return;
  }

  switch (tilstand) {
  case Modtag_Adresse:
    if (character == ':') {
      if (KonverterAdresse(&modtaget_adresse)) {
        valgt_adresse = (volatile uint8_t *)(uintptr_t)modtaget_adresse;
        SkiftTilstand(Modtag_Bit_Position_I_Adresse);
      } else {
        RydAdresse();
        SkiftTilstand(Modtag_Adresse);
      }
    } else if (adresse_laengde < (sizeof(adresse_tekst) - 1U) &&
               (HexVaerdi(character, &hex_vaerdi) ||
                (adresse_laengde == 1 && adresse_tekst[0] == '0' &&
                 (character == 'x' || character == 'X')))) {
      adresse_tekst[adresse_laengde++] = character;
      adresse_tekst[adresse_laengde] = '\0';
    } else {
      RydAdresse();
      SkiftTilstand(Modtag_Adresse);
    }
    break;

  case Modtag_Bit_Position_I_Adresse:
    if (character >= '0' && character <= '7') {
      bit_position = (uint8_t)(character - '0');
      SkiftTilstand(Modtag_Bit_Positon_Vaerdi);
    } else {
      RydAdresse();
      SkiftTilstand(Modtag_Adresse);
    }
    break;

  case Modtag_Bit_Positon_Vaerdi:
    if (character == '0' || character == '1') {
      uint8_t maske = (uint8_t)(1U << bit_position);
      if (character == '1') {
        *valgt_adresse |= maske;
      } else {
        *valgt_adresse &= (uint8_t)~maske;
      }
      UdskrivResultat((uint8_t)(character - '0'));
    }
    RydAdresse();
    SkiftTilstand(Modtag_Adresse);
    break;
  }
}

ISR(USART0_RX_vect) {
  uint8_t naeste_skriveindeks =
      (uint8_t)((uart0_write + 1U) & UART0_QUEUE_MASK);
  char character = UDR0;

  if (naeste_skriveindeks != uart0_read) {
    uart0_queue[uart0_write] = character;
    uart0_write = naeste_skriveindeks;
  }
}

int main(void) {
  UART0_Init();
  RydAdresse();
  SkiftTilstand(Modtag_Adresse);
  sei();

  while (1) {
    if (uart0_read != uart0_write) {
      char character = uart0_queue[uart0_read];
      uart0_read = (uint8_t)((uart0_read + 1U) & UART0_QUEUE_MASK);
      BehandlTegn(character);
    }
  }
}
