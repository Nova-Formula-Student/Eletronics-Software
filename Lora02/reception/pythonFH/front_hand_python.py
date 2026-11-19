import sys
import random
from PySide6.QtWidgets import QApplication, QWidget, QLabel, QVBoxLayout 
from PySide6.QtCore import Qt, QTimer
import pyqtgraph as pg

app = QApplication(sys.argv)

janela = QWidget()
janela.resize(800, 700)
layout = QVBoxLayout(janela)

texto_vel = QLabel("-- Km/h")
texto_temp = QLabel("-- ºC")

for text in (texto_vel, texto_temp):
    text.setAlignment(Qt.AlignCenter)
    text.setStyleSheet("font-size: 60px; font-weight: bold;")
    layout.addWidget(text)

grafico = pg.PlotWidget(title="Velocidade")
curva = grafico.plot(pen="y")
layout.addWidget(grafico)

historico = []

def atualizar():
    velocidade = random.uniform(0, 100)
    temperatura = random.uniform(40, 90)

    texto_vel.setText(f"{velocidade:.1f} km/h")
    texto_temp.setText(f"{temperatura:.1f} ºC")

    historico.append(velocidade)
    if len(historico) > 100:
        historico.pop(0)
    curva.setData(historico)
 
    if temperatura > 65:
        cor = "red"
    else:
        cor ="green"

    texto_temp.setStyleSheet(f"font-size: 60px; font-weight: bold; color: {cor};")


timer = QTimer()
timer.timeout.connect(atualizar)
timer.start(500)

janela.show()
sys.exit(app.exec())


#colocar fundo preto e quadrados para as cenas da temperatura e dos km e ver como são na f1
